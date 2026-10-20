// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */

#include "linux/cnum.h"
#include <linux/bpf_verifier.h>
#include <linux/jhash.h>
#include <linux/log2.h>
#include <linux/math64.h>
#include <linux/min_heap.h>
#include <linux/bug.h>
#include <linux/tnum.h>
#include <linux/overflow.h>

#define REGS_NUM BPF_SCEV_REGS_NUM
#define UNKNOWN_EXPR_ID 0
#define OPAQUE_EXPR_ID  1

/*
 * BPF instructions use 'code', 'src_reg', 'off' and 'imm' fields for instruction encoding.
 * For scalar evolution purpose we want to reuse most of the opcode definitions,
 * but also add a few custom operations (REG and IMM).
 * Use the enum below to uniformly represent all operations relevant for SCEV.
 */
enum expr_op {
	/* leave range 0..255 for standard bpf opcodes */
	UNKNOWN = 256, /* start custom opcodes from the second byte */
	REG,
	IMM,
	SDIV, SMOD,
	SEXT8, SEXT16, SEXT32,
	ZEXT8, ZEXT16, ZEXT32,
	BSWAP16, BSWAP32, BSWAP64,
	/* during the loop body execution the value can be either of param[0] or param[1] */
	ANY,
	/* some value that verifier is not going to track precisely */
	OPAQUE,
	/* '*(u8/16/32 *)(r10 + X) = Y' writes define slot contents only partially */
	SPILL8, SPILL16, SPILL32,
	/*
	 * SCEV expression corresponding to linear equation 'param[0] + param[1] * k',
	 * where k is a loop iteration number. Loop here refers to innermost loop
	 * containing instruction associated with this expression, as returned by
	 * bpf_loop_at_index().
	 */
	LINEAR_SCEV,
};

struct expr {
	u32 op;
	union {
		u32 params[2];
		s64 imm;
	};
};

struct expr_bucket {
	u32 cnt;
	u32 cap;
	u32 ids[];
};

struct env {
	bool empty;
	u32 reg2expr[REGS_NUM];
	u32 reg2scev[REGS_NUM];
};

struct insn_envs {
	u32 cnt;
	struct {
		int loop_header;
		struct env *env;
	} entries[];
};

#define EXPR_STACK_DEPTH 8

struct expr_stack_elt {
	u32 id:28;
	u32 pre:1;
	u32 next_param:2;
};

DEFINE_MIN_HEAP(int, scev_worklist);

struct scev {
	/* Read-only representatives for stack slots without a saved register. */
	struct bpf_reg_state zero_reg;
	struct bpf_reg_state unknown_reg;
	/*
	 * Expressions are identified by id, exprs_ht ensures that
	 * each expression exists as a unique instance.
	 * This allows for fast equivalence check: id1 === id2.
	 */
	struct expr_bucket **exprs_ht;
	struct scev_worklist worklist;
	struct insn_envs **envs;
	struct expr *exprs;
	/*
	 * Loops are analyzed one by one, this array keeps track if a particular
	 * instruction was visited on a current pass.
	 */
	u32 *discovered;
	u32 exprs_ht_cnt;
	int exprs_cnt;
	int exprs_cap;
	int envs_cnt;
	int stack_sz;
	struct expr_stack_elt expr_stack[EXPR_STACK_DEPTH];
	u32 ids_buf[EXPR_STACK_DEPTH];
};

static u32 expr_hash(struct expr *e)
{
	return jhash_3words(e->op, e->params[0], e->params[1], 0);
}

static int expr_eq(struct expr *a, struct expr *b)
{
	return a->op == b->op && a->imm == b->imm;
}

static int add_expr(struct scev *scev, struct expr e)
{
	struct expr_bucket *bucket;
	u32 i, id, hash, new_cap;
	void *tmp;

	hash = expr_hash(&e) & (scev->exprs_ht_cnt - 1);
	bucket = scev->exprs_ht[hash];

	if (bucket) {
		for (i = 0; i < bucket->cnt; i++) {
			id = bucket->ids[i];
			if (expr_eq(&e, &scev->exprs[id]))
				return id;
		}
	}

	if (!bucket || bucket->cap == bucket->cnt) {
		new_cap = bucket ? bucket->cap * 2 : 4;
		bucket = kvrealloc(bucket, sizeof(*bucket) + sizeof(u32) * new_cap, GFP_KERNEL_ACCOUNT | __GFP_ZERO);
		if (!bucket)
			return -ENOMEM;
		scev->exprs_ht[hash] = bucket;
		bucket->cap = new_cap;
	}

	if (scev->exprs_cnt == scev->exprs_cap) {
		new_cap = scev->exprs_cap ? scev->exprs_cap * 2 : 256;
		tmp = kvrealloc(scev->exprs, sizeof(struct expr) * new_cap, GFP_KERNEL_ACCOUNT);
		if (!tmp)
			return -ENOMEM;
		scev->exprs = tmp;
		scev->exprs_cap = new_cap;
	}

	id = scev->exprs_cnt++;
	scev->exprs[id] = e;
	bucket->ids[bucket->cnt++] = id;
	return id;
}

static int expr2(struct scev *scev, u32 op, int a, int b)
{
	if (a < 0)
		return a;
	if (b < 0)
		return b;
	return add_expr(scev, (struct expr){ .op = op, .params = {a, b} });
}

static int expr1(struct scev *scev, u32 op, int a)
{
	if (a < 0)
		return a;
	return expr2(scev, op, a, 0);
}

static int expr0(struct scev *scev, u32 op)
{
	return expr2(scev, op, 0, 0);
}

static int imm_expr(struct scev *scev, s64 value)
{
	return add_expr(scev, (struct expr){ .op = IMM, .imm = value });
}

static bool same_exprs(struct scev *scev, int id_a, int id_b)
{
	return id_a == id_b;
}

static bool is_imm(struct scev *scev, int id, s64 *imm)
{
	if (scev->exprs[id].op != IMM)
		return false;
	*imm = scev->exprs[id].imm;
	return true;
}

static bool is_op(struct scev *scev, int id, u32 op)
{
	if (scev->exprs[id].op != op)
		return false;
	return true;
}

static bool is_unop(struct scev *scev, enum expr_op op, int id, u32 *p0)
{
	if (scev->exprs[id].op != op)
		return false;
	*p0 = scev->exprs[id].params[0];
	return true;
}

static bool is_binop(struct scev *scev, enum expr_op op, int id, u32 *p0, u32 *p1)
{
	if (scev->exprs[id].op != op)
		return false;
	*p0 = scev->exprs[id].params[0];
	*p1 = scev->exprs[id].params[1];
	return true;
}

static bool is_reg(struct scev *scev, int id, u32 *reg)
{
	return is_unop(scev, REG, id, reg);
}

static bool is_add(struct scev *scev, int id, u32 *left, u32 *right)
{
	return is_binop(scev, BPF_ADD, id, left, right);
}

static bool is_zext32(struct scev *scev, int id, u32 *left)
{
	return is_unop(scev, ZEXT32, id, left);
}

static bool is_any(struct scev *scev, int id, u32 *left, u32 *right)
{
	return is_binop(scev, ANY, id, left, right);
}

static bool is_linear(struct scev *scev, int id, u32 *base, u32 *slope)
{
	return is_binop(scev, LINEAR_SCEV, id, base, slope);
}

static bool is_linear_or_linear32(struct scev *scev, int id, u32 *base, u32 *slope, bool *zext32)
{
	u32 t;

	*zext32 = is_zext32(scev, id, &t);
	return is_linear(scev, *zext32 ? t : id, base, slope);
}

static bool is_opaque(struct scev *scev, int id)
{
	return is_op(scev, id, OPAQUE);
}

static void log_reg(struct bpf_verifier_env *env, u32 reg)
{
	if (reg < MAX_BPF_REG)
		bpf_log(&env->log, "r%d", reg);
	else
		bpf_log(&env->log, "*fp%d", (MAX_BPF_REG - reg - 1) * 8);
}

static const char *op_str(u32 op)
{
	switch (op) {
	case BPF_ADD:  return "+";
	case BPF_SUB:  return "-";
	case BPF_MUL:  return "*";
	case BPF_DIV:  return "/";
	case SDIV:     return "s/";
	case BPF_OR:   return "|";
	case BPF_AND:  return "&";
	case BPF_LSH:  return "<<";
	case BPF_RSH:  return ">>";
	case BPF_NEG:  return "-";
	case BPF_MOD:  return "%";
	case SMOD:     return "s%";
	case BPF_XOR:  return "^";
	case BPF_ARSH: return "s>>";
	case SEXT8:    return "sext8";
	case SEXT16:   return "sext16";
	case SEXT32:   return "sext32";
	case ZEXT8:    return "zext8";
	case ZEXT16:   return "zext16";
	case ZEXT32:   return "zext32";
	case BSWAP16:  return "bswap16";
	case BSWAP32:  return "bswap32";
	case BSWAP64:  return "bswap64";
	case SPILL8:   return "spill8";
	case SPILL16:  return "spill16";
	case SPILL32:  return "spill32";
	case ANY:      return "any";
	case LINEAR_SCEV:  return "linear";
	}
	return NULL;
}

static u32 op_params_num(u32 op)
{
	switch (op) {
	case BPF_ADD:
	case BPF_SUB:
	case BPF_MUL:
	case BPF_DIV:
	case BPF_MOD:
	case BPF_OR:
	case BPF_XOR:
	case BPF_AND:
	case BPF_LSH:
	case BPF_RSH:
	case BPF_ARSH:
	case SDIV:
	case SMOD:
	case ANY:
	case LINEAR_SCEV:
		return 2;
	case BPF_NEG:
	case SEXT8:
	case SEXT16:
	case SEXT32:
	case ZEXT8:
	case ZEXT16:
	case ZEXT32:
	case BSWAP16:
	case BSWAP32:
	case BSWAP64:
	case SPILL8:
	case SPILL16:
	case SPILL32:
		return 1;
	case REG:
	case IMM:
	case OPAQUE:
		return 0;
	default:
		return 0;
	}
}

static bool expr_stack_push(struct scev *scev, u32 id)
{
	if (scev->stack_sz >= EXPR_STACK_DEPTH)
		return false;
	scev->expr_stack[scev->stack_sz].id = id;
	scev->expr_stack[scev->stack_sz].pre = true;
	scev->expr_stack[scev->stack_sz].next_param = 0;
	scev->stack_sz++;
	return true;
}

enum {
	PRE = BIT(1), POST = BIT(2), DEPTH_LIMIT = BIT(3)
};

static bool expr_next(struct scev *scev, u32 *id, u32 *order)
{
	struct expr_stack_elt *elt;
	struct expr *expr;
	u32 num_params;

	if (scev->stack_sz == 0)
		return false;

	elt = &scev->expr_stack[scev->stack_sz - 1];
	*id = elt->id;
	*order = 0;
	expr = &scev->exprs[elt->id];
	num_params = op_params_num(expr->op);
	if (elt->pre) {
		elt->pre = false;
		*order = PRE;
		return true;
	}
	if (elt->next_param == num_params) {
		*order = POST;
		scev->stack_sz--;
		return true;
	}
	if (scev->stack_sz == EXPR_STACK_DEPTH) {
		*order = POST | DEPTH_LIMIT;
		scev->stack_sz--;
		return true;
	}
	expr_stack_push(scev, expr->params[elt->next_param]);
	elt->next_param++;
	return expr_next(scev, id, order);
}

static void log_expr(struct bpf_verifier_env *env, u32 id)
{
	struct bpf_verifier_log *log = &env->log;
	struct scev *scev = env->scev;
	struct expr *expr;
	const char *str;
	u32 order;

	scev->stack_sz = 0;
	expr_stack_push(scev, id);
	while (expr_next(scev, &id, &order)) {
		if ((order & PRE) && scev->stack_sz > 1)
			bpf_log(log, " ");
		expr = &scev->exprs[id];
		switch (expr->op) {
		case UNKNOWN:
			if (order & PRE)
				bpf_log(log, "?");
			break;
		case OPAQUE:
			if (order & PRE)
				bpf_log(log, "_");
			break;
		case REG:
			if (order & PRE)
				log_reg(env, expr->params[0]);
			break;
		case IMM:
			if (order & PRE)
				bpf_log(log, "%lld", expr->imm);
			break;
		default:
			if (order & PRE) {
				str = op_str(expr->op);
				bpf_log(log, "(");
				if (str)
					bpf_log(log, "%s", str);
				else
					bpf_log(log, "bad-expr-op %x", expr->op);
			}
			if (order & DEPTH_LIMIT)
				bpf_log(log, "...");
			if (order & POST)
				bpf_log(log, ")");
		}
	}
}

static bool reg_alive_at(struct bpf_verifier_env *env, u32 reg, u32 insn_idx)
{
	struct bpf_insn_aux_data *aux = env->insn_aux_data;
	u16 live_regs = aux[insn_idx].live_regs_before;

	return reg < MAX_BPF_REG
	       ? (BIT(reg) & live_regs)
	       : test_bit(reg - MAX_BPF_REG, aux[insn_idx].live_stack_before);
}

static void print_env(struct bpf_verifier_env *env, struct env *e, u32 insn_idx)
{
	struct bpf_verifier_log *log = &env->log;
	struct scev *scev = env->scev;
	bool printed_some = false;
	bool is_self_scev;
	bool is_self_reg;
	int i, r;

	if (e->empty) {
		bpf_log(log, "  <empty>\n");
		return;
	}

	for (i = 0; i < REGS_NUM; i++) {
		if (!reg_alive_at(env, i, insn_idx))
			continue;
		is_self_reg = is_reg(scev, e->reg2expr[i], &r) && i == r;
		is_self_scev = is_reg(scev, e->reg2scev[i], &r) && i == r;
		if (is_self_reg && is_self_scev)
			continue;
		printed_some = true;
		bpf_log(log, "  ");
		log_reg(env, i);
		bpf_log(log, "=");
		log_expr(env, e->reg2expr[i]);
		if (e->reg2expr[i] != UNKNOWN_EXPR_ID) {
			bpf_log(log, " / ");
			log_expr(env, e->reg2scev[i]);
		}
		bpf_log(log, "\n");
	}
	if (!printed_some)
		bpf_log(log, "  <all regs unchanged>\n");
}
static struct env *find_loop_env(struct scev *scev, int loop_header, int insn_idx)
{
	struct insn_envs *envs = scev->envs[insn_idx];
	u32 i;

	if (!envs)
		return NULL;
	for (i = 0; i < envs->cnt; i++)
		if (envs->entries[i].loop_header == loop_header)
			return envs->entries[i].env;
	return NULL;
}

static struct env *find_header_env(struct scev *scev, int header)
{
	return find_loop_env(scev, header, header);
}

static struct env *get_loop_env(struct scev *scev, int loop_header, int insn_idx)
{
	struct insn_envs *envs = scev->envs[insn_idx];
	u32 cnt = envs ? envs->cnt : 0;
	struct insn_envs *tmp;
	struct env *e;

	e = find_loop_env(scev, loop_header, insn_idx);
	if (e)
		return e;

	e = kzalloc(sizeof(*e), GFP_KERNEL_ACCOUNT);
	if (!e)
		return NULL;

	tmp = krealloc(envs, struct_size(envs, entries, cnt + 1), GFP_KERNEL_ACCOUNT);
	if (!tmp) {
		kfree(e);
		return NULL;
	}

	e->empty = true;
	tmp->entries[cnt].loop_header = loop_header;
	tmp->entries[cnt].env = e;
	tmp->cnt = cnt + 1;
	scev->envs[insn_idx] = tmp;
	return e;
}

static int replace_reg(struct scev *scev, struct env *e, u32 reg, int id)
{
	if (id < 0)
		return id;
	e->reg2expr[reg] = id;
	return 0;
}

static int setup_initial_loop_env(struct bpf_verifier_env *env, struct env *e, int insn_idx)
{
	struct scev *scev = env->scev;
	int i, err;

	/*
	 * Initial assignments are of form rX -> rX are needed as seeds for subsequent
	 * work queue pop / transfer() calls. Because e->empty is true, the first backedge
	 * join() will overwrite the them. compute_header_scevs() accounts for register
	 * states at loop entry.
	 */
	for (i = 0; i < REGS_NUM; i++) {
		err = replace_reg(scev, e, i, expr1(scev, REG, i));
		if (err)
			return err;
	}
	return 0;
}

static void forget_call_regs(struct env *e)
{
	int i;

	for (i = BPF_REG_0; i <= BPF_REG_5; i++)
		e->reg2expr[i] = UNKNOWN_EXPR_ID;
}

static u32 spill_spi(struct bpf_insn *insn)
{
	return -insn->off / BPF_REG_SIZE - 1;
}

static u32 off_to_reg(struct bpf_insn *insn)
{
	return spill_spi(insn) + MAX_BPF_REG;
}

static bool is_spill_off(int off)
{
	return off % BPF_REG_SIZE == 0 &&
	       off <= -BPF_REG_SIZE &&
	       off >= -MAX_BPF_STACK_JIT;
}

static void mark_opaque(struct env *e, int off, int size)
{
	int b, spi;

	for (b = off; b < off + size; b++) {
		if (b >= 0 || b < -MAX_BPF_STACK_JIT)
			continue;
		spi = (-b - 1) / BPF_REG_SIZE;
		e->reg2expr[MAX_BPF_REG + spi] = OPAQUE_EXPR_ID;
	}
}

static int mk_spill(struct scev *scev, u8 size, int id)
{
	switch (size) {
	case BPF_B:  return expr1(scev, SPILL8,  id);
	case BPF_H:  return expr1(scev, SPILL16, id);
	case BPF_W:  return expr1(scev, SPILL32, id);
	case BPF_DW: return id;
	}
	return UNKNOWN_EXPR_ID;
}

static int mk_fill(struct scev *scev, int id, u8 code)
{
	bool sx = BPF_MODE(code) == BPF_MEMSX;

#ifdef __BIG_ENDIAN
	/* A narrow fill from a DW spill reads high bits on big-endian. */
	if (BPF_SIZE(code) != BPF_DW)
		return OPAQUE_EXPR_ID;
#endif

	switch (BPF_SIZE(code)) {
	case BPF_B:  return expr1(scev, sx ? SEXT8  : ZEXT8,  id);
	case BPF_H:  return expr1(scev, sx ? SEXT16 : ZEXT16, id);
	case BPF_W:  return expr1(scev, sx ? SEXT32 : ZEXT32, id);
	case BPF_DW: return sx ? UNKNOWN_EXPR_ID : id; /* DW sign-extended load is invalid */
	}
	return UNKNOWN_EXPR_ID;
}

static int maybe_store_fp(struct scev *scev, struct env *e, struct bpf_insn *insn, int id)
{
	u8 size = BPF_SIZE(insn->code);

	if (insn->dst_reg != BPF_REG_FP)
		return 0;
	if (is_spill_off(insn->off))
		return replace_reg(scev, e, off_to_reg(insn), mk_spill(scev, size, id));
	mark_opaque(e, insn->off, bpf_size_to_bytes(size));
	return 0;
}

static int maybe_load_fp(struct scev *scev, struct env *e, struct bpf_insn *insn)
{
	int id;

	if (insn->src_reg == BPF_REG_FP && is_spill_off(insn->off))
		id = mk_fill(scev, e->reg2expr[off_to_reg(insn)], insn->code);
	else
		id = OPAQUE_EXPR_ID;
	return replace_reg(scev, e, insn->dst_reg, id);
}

static int transfer(struct bpf_verifier_env *env, struct env *e, int idx)
{
	const bool little_endian = htons(0x3412) == 0x1234;
	struct bpf_insn *insn = &env->prog->insnsi[idx];
	struct scev *scev = env->scev;
	u32 *reg2expr = e->reg2expr;
	u8 class = BPF_CLASS(insn->code);
	u8 x_or_k = BPF_SRC(insn->code);
	u8 opcode = BPF_OP(insn->code);
	u8 mode = BPF_MODE(insn->code);
	u32 dst = insn->dst_reg;
	u32 src = insn->src_reg;
	u32 op, sext;
	bool need_bswap;
	int i, id;
	s64 imm;

	switch (class) {
	case BPF_ALU:
	case BPF_ALU64:
		switch (opcode) {
		case BPF_MOV:
			switch (insn->off) {
			case 0: sext = 0; break;
			case 8: sext = SEXT8; break;
			case 16: sext = SEXT16; break;
			case 32: sext = SEXT32; break;
			default:
				goto mark_dst_unknown;
			}

			if (x_or_k == BPF_X && insn->imm == 0)
				id = reg2expr[src];
			else if (x_or_k == BPF_K && src == 0 && insn->off == 0)
				id = imm_expr(scev, insn->imm);
			else
				goto mark_dst_unknown;

			if (sext)
				id = expr1(scev, sext, id);
			if (class == BPF_ALU)
				id = expr1(scev, ZEXT32, id);

			return replace_reg(scev, e, dst, id);

		case BPF_ADD:
		case BPF_SUB:
		case BPF_MUL:
		case BPF_DIV:
		case BPF_MOD:
		case BPF_OR:
		case BPF_XOR:
		case BPF_AND:
		case BPF_LSH:
		case BPF_RSH:
		case BPF_ARSH:
			if (opcode == BPF_DIV && insn->off == 1)
				op = SDIV;
			else if (opcode == BPF_MOD && insn->off == 1)
				op = SMOD;
			else if (insn->off == 0)
				op = opcode;
			else
				goto mark_dst_unknown;

			if (x_or_k == BPF_X && insn->imm == 0)
				id = reg2expr[src];
			else if (x_or_k == BPF_K && src == 0)
				id = imm_expr(scev, insn->imm);
			else
				goto mark_dst_unknown;

			id = expr2(scev, op, reg2expr[dst], id);

			if (class == BPF_ALU)
				id = expr1(scev, ZEXT32, id);

			return replace_reg(scev, e, dst, id);

		case BPF_NEG:
			if (src == 0 && insn->off == 0 && insn->imm == 0)
				id = expr1(scev, BPF_NEG, reg2expr[dst]);
			else
				goto mark_dst_unknown;

			if (class == BPF_ALU)
				id = expr1(scev, ZEXT32, id);

			return replace_reg(scev, e, dst, id);

		case BPF_END:
			if (src != 0 || insn->off != 0 || (class == BPF_ALU64 && x_or_k != 0))
				goto mark_dst_unknown;

			need_bswap = class == BPF_ALU64 || ((x_or_k == BPF_TO_LE) != little_endian);
			switch (insn->imm) {
			case 16: op = need_bswap ? BSWAP16 : ZEXT16; break;
			case 32: op = need_bswap ? BSWAP32 : ZEXT32; break;
			case 64: op = need_bswap ? BSWAP64 : 0; break;
			default:
				goto mark_dst_unknown;
			}

			id = reg2expr[dst];
			if (op)
				id = expr1(scev, op, reg2expr[dst]);

			return replace_reg(scev, e, dst, id);
		default:
			goto mark_dst_unknown;
		}
		break;
	case BPF_LDX:
		switch (mode) {
		case BPF_MEM:
		case BPF_MEMSX:
			return maybe_load_fp(scev, e, insn);
		default:
			goto mark_dst_unknown;
		}
	case BPF_STX:
		switch (mode) {
		case BPF_MEM:
			return maybe_store_fp(scev, e, insn, reg2expr[src]);
		case BPF_ATOMIC:
			if (insn->imm == BPF_LOAD_ACQ)
				return maybe_load_fp(scev, e, insn);
			if (insn->imm == BPF_STORE_REL) {
				return maybe_store_fp(scev, e, insn, reg2expr[src]);
			}
			/*
			 * verifier does not track other atomic ops precisely,
			 * hence mark the results as opaque.
			 */
			if (insn->dst_reg == BPF_REG_FP)
				mark_opaque(e, insn->off, bpf_size_to_bytes(BPF_SIZE(insn->code)));
			if (insn->imm == BPF_CMPXCHG)
				return replace_reg(scev, e, BPF_REG_0, OPAQUE_EXPR_ID);
			if (insn->imm & BPF_FETCH)
				return replace_reg(scev, e, src, OPAQUE_EXPR_ID);
			break;
		}
		break;
	case BPF_ST:
		if (insn->dst_reg == BPF_REG_FP) {
			id = imm_expr(scev, insn->imm);
			if (id < 0)
				return id;
			return maybe_store_fp(scev, e, insn, id);
		}
		break;
	case BPF_JMP:
	case BPF_JMP32:
		if (opcode == BPF_CALL)
			forget_call_regs(e);
		/* for non-CALL there are no changes in register states */
		break;
	case BPF_LD:
		switch (mode) {
		case BPF_IMM:
			/* rX = imm ll */
			if (BPF_SIZE(insn->code) == BPF_DW && insn->src_reg == 0) {
				imm = ((u64)(insn + 1)->imm << 32) | (u32)insn->imm;
				return replace_reg(scev, e, dst, imm_expr(scev, imm));
			}
			/* map, map value, BTF id, function */
			if (BPF_SIZE(insn->code) == BPF_DW)
				return replace_reg(scev, e, dst, OPAQUE_EXPR_ID);
			goto mark_dst_unknown;
		case BPF_ABS:
		case BPF_IND:
			forget_call_regs(e);
			break;
		default:
			goto mark_dst_unknown;
		}
		break;
	default:
		/* unknown instruction, nuke state */
		for (i = 0; i < REGS_NUM; i++)
			reg2expr[i] = UNKNOWN_EXPR_ID;
		break;
	}
	return 0;

mark_dst_unknown:
	reg2expr[dst] = UNKNOWN_EXPR_ID;
	return 0;
}

/*
 * Construct minimal 'ANY' expression by traversing 'a' and 'b'
 * and accumulating non-duplicated non-ANY entries.
 */
static int mk_any(struct scev *scev, int a, int b)
{
	struct expr_stack_elt *elt;
	struct expr *expr;
	u32 i, j, ids_buf_sz;
	u32 roots[2] = {a, b};
	int id;

	if (a < 0)
		return a;
	if (b < 0)
		return b;
	ids_buf_sz = 0;
	for (i = 0; i < ARRAY_SIZE(roots); i++) {
		scev->stack_sz = 0;
		expr_stack_push(scev, roots[i]);
		while (scev->stack_sz) {
			elt = &scev->expr_stack[--scev->stack_sz];
			id = elt->id;
			expr = &scev->exprs[elt->id];
			if (expr->op == ANY) {
				if (!expr_stack_push(scev, expr->params[1]) ||
				    !expr_stack_push(scev, expr->params[0]))
					return UNKNOWN_EXPR_ID;
			} else {
				for (j = 0; j < ids_buf_sz; j++) {
					if (same_exprs(scev, scev->ids_buf[j], id))
						goto next;
				}
				if (ids_buf_sz == ARRAY_SIZE(scev->ids_buf))
					return UNKNOWN_EXPR_ID;
				scev->ids_buf[ids_buf_sz++] = id;
			}
next:;
		}
	}
	if (WARN_ON(ids_buf_sz == 0))
		return -EFAULT;
	id = scev->ids_buf[0];
	for (i = 1; i < ids_buf_sz; i++) {
		id = expr2(scev, ANY, id, scev->ids_buf[i]);
		if (id < 0)
			return id;
	}
	return id;
}

static int join(struct scev *scev, struct env *acc, struct env *cur)
{
	int i, id;

	if (acc->empty) {
		memcpy(acc, cur, sizeof(*acc));
		acc->empty = false;
		return 0;
	}

	for (i = 0; i < REGS_NUM; i++) {
		if (!same_exprs(scev, acc->reg2expr[i], cur->reg2expr[i])) {
			id = mk_any(scev, acc->reg2expr[i], cur->reg2expr[i]);
			if (id < 0)
				return id;
			acc->reg2expr[i] = id;
		}
	}
	return 0;
}

/* Mark any register modified in 'e' as unknown in 'acc'. */
static void forget_modified_regs(struct scev *scev, struct env *acc, struct env *e)
{
	u32 i, reg;

	for (i = 0; i < REGS_NUM; i++) {
		if (e && is_reg(scev, e->reg2expr[i], &reg) && reg == i)
			continue;
		acc->reg2expr[i] = UNKNOWN_EXPR_ID;
	}
}

/* For a loop H at `header`, mark any register modified as a result of H execution as unknown. */
static void forget_non_invariants(struct bpf_verifier_env *env, struct env *acc, int header)
{
	struct bpf_insn_aux_data *aux = env->insn_aux_data;
	struct bpf_loop *loop = aux[header].loop;
	struct scev *scev = env->scev;
	int i, idx, h;

	acc->empty = false;
	forget_modified_regs(scev, acc, find_header_env(scev, header));
	for (i = 0; i < loop->exits_cnt; i++) {
		idx = loop->exits[i].from;
		h = bpf_loop_at_index(env, idx);
		/*
		 * In order to account for situations like this:
		 *
		 * header:
		 *   do {
		 *	saved = rX;
		 *	rX = -1000;
		 *	do {				// H
		 *		if (random())
		 *			goto out;	// E
		 *	} while (inner_again);
		 *	rX = saved;
		 *    } while (outer_again);
		 *  out:
		 *
		 * For exits originating in nested loops check that rX is invariant
		 * both at the header (H) and at exit (E).
		 */
		while (h != header) {
			forget_modified_regs(scev, acc, find_header_env(scev, h));
			forget_modified_regs(scev, acc, find_loop_env(scev, h, idx));
			idx = h;
			h = aux[h].loop_header;
		}
		/*
		 * For exits originating at the same loop the header is already accounted for,
		 * check only the exit environment.
		 */
		forget_modified_regs(scev, acc, find_loop_env(scev, header, idx));
	}
}

/* Order worklist in reverse post-order. */
static bool reverse_postorder_less(const void *a, const void *b, void *arg)
{
	const int *rank = arg;

	return rank[*(const int *)a] > rank[*(const int *)b];
}

static const struct min_heap_callbacks worklist_cb = {
	.less = reverse_postorder_less,
};

static int worklist_push(struct bpf_verifier_env *env, int loop_header, int idx)
{
	struct scev *scev = env->scev;
	struct scev_worklist *worklist = &scev->worklist;
	u32 tag = (u32)loop_header + 1;

	if (scev->discovered[idx] == tag)
		return 0;

	if (min_heap_full_inline(worklist)) {
		size_t size = worklist->size ? worklist->size * 2 : 16;
		int *data;

		data = kvrealloc(worklist->data, array_size(size, sizeof(*data)),
				GFP_KERNEL_ACCOUNT);
		if (!data)
			return -ENOMEM;
		worklist->data = data;
		worklist->size = size;
	}
	if (!min_heap_push_inline(worklist, &idx, &worklist_cb, env->cfg.postorder_nums))
		return -EFAULT;

	scev->discovered[idx] = tag;
	return 0;
}

static bool reg_alive_at_succ(struct bpf_verifier_env *env, u32 r, u32 insn_idx)
{
	struct bpf_iarray *succ = bpf_insn_successors(env, insn_idx);
	u32 i;

	for (i = 0; i < succ->cnt; i++)
		if (reg_alive_at(env, r, succ->items[i]))
			return true;
	return false;
}

enum changes_at {
	LOG_AT_TRANSFER,
	LOG_AT_JOIN,
};

static void log_env_changes(struct bpf_verifier_env *env, enum changes_at at,
			    struct env *old, struct env *new, int insn_idx)
{
	struct bpf_verifier_log *log = &env->log;
	struct scev *scev = env->scev;
	u64 null_pos = log->end_pos;
	u32 old_id, new_id, regno;
	bool any_changes = false;
	bool is_self_scev;
	u64 len;
	int r;

	bpf_log(log, "%s %4d: ", at == LOG_AT_TRANSFER ? "t" : "j", insn_idx);
	bpf_verbose_insn(env, &env->prog->insnsi[insn_idx]);
	len = log->end_pos - null_pos;
	bpf_log(log, "%*s", max(37 - (int)len, 1), " ");
	bpf_log(log, " ; ");
	for (r = 0; r < REGS_NUM; r++) {
		old_id = old->reg2expr[r];
		new_id = new->reg2expr[r];
		if (at == LOG_AT_TRANSFER && !reg_alive_at_succ(env, r, insn_idx))
			continue;
		if (at == LOG_AT_JOIN && !reg_alive_at(env, r, insn_idx))
			continue;
		if (same_exprs(scev, old_id, new_id))
			continue;
		is_self_scev = is_reg(scev, new_id, &regno) && r == regno;
		if (old->empty && is_self_scev)
			continue;
		if (any_changes)
			bpf_log(log, ", ");
		log_reg(env, r);
		bpf_log(log, " ");
		log_expr(env, old_id);
		bpf_log(log, " -> ");
		log_expr(env, new_id);
		any_changes = true;
	}
	if (old->empty && !any_changes)
		bpf_log(log, "<empty join>");
	bpf_log(log, "\n");
	if (!old->empty && !any_changes)
		bpf_vlog_reset(log, null_pos);
}

static bool is_probe_read_helper(u32 func_id)
{
	return func_id == BPF_FUNC_probe_read ||
	       func_id == BPF_FUNC_probe_read_kernel ||
	       func_id == BPF_FUNC_probe_read_user ||
	       func_id == BPF_FUNC_probe_read_str ||
	       func_id == BPF_FUNC_probe_read_kernel_str ||
	       func_id == BPF_FUNC_probe_read_user_str;
}

/*
 * If instruction is an indirect write to stack, invalidate SCEVs for spi's
 * that this instruction can touch.
 */
static void reset_scevs_at_indirect_writes(struct bpf_verifier_env *env, struct env *cur_env, int idx)
{
	struct bpf_insn *insn = &env->prog->insnsi[idx];
	struct scev *scev = env->scev;
	u8 class = BPF_CLASS(insn->code);
	const unsigned long *mask;
	u32 spi, reg;
	bool opaque;

	/* Direct fp stores are fine. */
	if ((class == BPF_ST || class == BPF_STX) && insn->dst_reg == BPF_REG_FP)
		return;

	mask = bpf_may_write_mask(env, idx);
	opaque = bpf_helper_call(insn) && is_probe_read_helper(insn->imm);
	for_each_set_bit(spi, mask, MAX_BPF_STACK_SLOTS) {
		reg = MAX_BPF_REG + spi;
		if (cur_env->reg2expr[reg] == UNKNOWN_EXPR_ID)
			continue;
		replace_reg(scev, cur_env, reg, opaque ? OPAQUE_EXPR_ID : UNKNOWN_EXPR_ID);
	}
}

/*
 * Collect loop-entry registers referenced by expr 'id', best effort.
 * DEPTH_LIMIT may leave dependencies unrecorded. This mask only avoids
 * widening that would lose stack-access precision; missing dependencies
 * may cause false rejections, but this is not a soundness issue.
 */
static void or_expr_regs(struct bpf_verifier_env *env, u32 id, unsigned long *mask)
{
	struct scev *scev = env->scev;
	u32 order;

	scev->stack_sz = 0;
	expr_stack_push(scev, id);
	while (expr_next(scev, &id, &order)) {
		if ((order & PRE) && scev->exprs[id].op == REG)
			__set_bit(scev->exprs[id].params[0], mask);
	}
}

/* Mask of argument registers (R1..R5) a call at 'idx' passes by register. */
static u16 call_params_mask(struct bpf_verifier_env *env, int idx)
{
	struct bpf_insn *insn = &env->prog->insnsi[idx];
	struct bpf_call_summary cs;
	int n = bpf_get_call_summary(env, insn, &cs) ? cs.arg_slot_cnt : MAX_BPF_FUNC_REG_ARGS;

	return n ? GENMASK(BPF_REG_1 + n - 1, BPF_REG_1) : 0;
}

/*
 * For instructions like:
 * - *(u64 *)(rBase + off) = rX
 * - rX = *(u64 *)(rBase + off)
 * - calls that construct objects on stack (e.g. dynptr_from_mem(rBase, ...))
 * When 'rBase' can be a stack pointer and is derived from some registers Rs
 * defined at loop entry, record Rs into 'mask'.
 */
static void collect_store_base_regs(struct bpf_verifier_env *env,
				    struct env *cur_env, int idx, unsigned long *mask)
{
	struct bpf_insn_aux_data *aux = env->insn_aux_data;
	struct bpf_insn *insn = &env->prog->insnsi[idx];
	u8 class = BPF_CLASS(insn->code);
	u8 size = BPF_SIZE(insn->code);
	u16 base_regs = 0;
	u32 r;

	if (size == BPF_W || size == BPF_DW) {
		if ((class == BPF_STX || class == BPF_ST) && insn->dst_reg != BPF_REG_FP)
			base_regs |= BIT(insn->dst_reg);
		else if (class == BPF_LDX && insn->src_reg != BPF_REG_FP)
			base_regs |= BIT(insn->src_reg);
	}

	if (class == BPF_JMP && BPF_OP(insn->code) == BPF_CALL &&
	    bpf_needs_fixed_stack_off(env, idx))
		base_regs |= call_params_mask(env, idx);

	base_regs &= aux[idx].stack_ptrs;
	for (r = 0; r < MAX_BPF_REG; r++)
		if (base_regs & BIT(r))
			or_expr_regs(env, cur_env->reg2expr[r], mask);
}

/* Find the topmost loop header containing idx inside cur_header, or -1 if none. */
static int topmost_nested_loop(struct bpf_verifier_env *env, int idx, int cur_header)
{
	struct bpf_insn_aux_data *aux = env->insn_aux_data;
	int header;

	for (header = bpf_loop_at_index(env, idx); header >= 0;
	     header = aux[header].loop_header)
		if (aux[header].loop_header == cur_header)
			return header;

	return -1;
}

/*
 * Join cur_env into the successor's environment and schedule their traversal.
 * Map successors in nested loops to their topmost nested header.
 * Ignore successors outside the current loop and its nested loops.
 */
static int join_successor(struct bpf_verifier_env *env, int cur_header, int succ_idx,
			  struct env *old_env, struct env *cur_env)
{
	bool log_level2 = env->log.level & BPF_LOG_LEVEL2;
	struct scev *scev = env->scev;
	int err, nested_header;
	struct env *succ_env;

	/*
	 * There are several possibilities for a successor:
	 * - succ_idx can be a part of a loop outside of the cur_header's loop,
	 *   such edges are ignored.
	 * - succ_idx can be a part of the same loop as cur_header,
	 *   for such edges succ_idx environment is updated:
	 *     e[succ_idx] = join(e[succ_idx], cur_env)
	 * - succ_idx can be a part of some loop inner to cur_header,
	 *   in such a case there exists some loop header H,
	 *   such that H.loop_header == cur_header
	 *   and H is the same as succ_idx's loop or contains it.
	 */
	if (bpf_loop_at_index(env, succ_idx) != cur_header) {
		/*
		 * topmost_nested_loop() either finds H or returns -1,
		 * in case if succ_idx is a part of a loop outer to cur_header.
		 */
		nested_header = topmost_nested_loop(env, succ_idx, cur_header);
		if (nested_header < 0)
			return 0;
		succ_idx = nested_header;
	}
	succ_env = get_loop_env(scev, cur_header, succ_idx);
	if (!succ_env)
		return -ENOMEM;
	if (log_level2)
		memcpy(old_env, succ_env, sizeof(*old_env));
	err = join(scev, succ_env, cur_env);
	if (err)
		return err;
	err = worklist_push(env, cur_header, succ_idx);
	if (err)
		return err;
	if (log_level2)
		log_env_changes(env, LOG_AT_JOIN, old_env, succ_env, succ_idx);
	return 0;
}

/* Unsupported loops get only an unknown header environment. */
static bool can_compute_loop_scev(const struct bpf_loop *loop)
{
	return !loop->irreducible && !loop->exits_overflow;
}

static int compute_scev_for_loop(struct bpf_verifier_env *env, int cur_header)
{
	struct scev_worklist *worklist = &env->scev->worklist;
	struct bpf_insn_aux_data *aux = env->insn_aux_data;
	struct bpf_loop *cur_loop = aux[cur_header].loop;
	struct env *header_env;
	struct scev *scev = env->scev;
	struct bpf_loop *nested_loop;
	struct env *cur_env = NULL;
	struct env *old_env = NULL;
	struct bpf_iarray *succ;
	bool log_level2 = env->log.level & BPF_LOG_LEVEL2;
	int s, i, err, idx, succ_idx;
	u32 r;

	if (log_level2)
		bpf_log(&env->log, "Computing SCEV for loop at %d:\n", cur_header);

	/*
	 * For irreducible loops, and for loops nesting a loop with a truncated
	 * exit list, just assume that everything is clobbered for now.
	 */
	if (!can_compute_loop_scev(cur_loop)) {
		header_env = get_loop_env(scev, cur_header, cur_header);
		if (!header_env)
			return -ENOMEM;
		/* The freshly allocated environment has all expressions unknown. */
		bitmap_fill(cur_loop->store_base_regs, REGS_NUM);
		header_env->empty = false;
		return 0;
	}

	cur_env = kzalloc(sizeof(*cur_env), GFP_KERNEL_ACCOUNT);
	if (!cur_env)
		goto nomem;
	if (log_level2) {
		old_env = kzalloc(sizeof(*old_env), GFP_KERNEL_ACCOUNT);
		if (!old_env)
			goto nomem;
	}
	header_env = get_loop_env(scev, cur_header, cur_header);
	if (!header_env)
		goto nomem;
	err = setup_initial_loop_env(env, header_env, cur_header);
	if (err)
		goto out;
	err = worklist_push(env, cur_header, cur_header);
	if (err)
		goto out;

	while (worklist->nr) {
		idx = *min_heap_peek_inline(worklist);
		min_heap_pop_inline(worklist, &worklist_cb, env->cfg.postorder_nums);

		/* join_successor() maps nested-loop entries to their representative header. */
		nested_loop = idx != cur_header ? aux[idx].loop : NULL;
		memcpy(cur_env, find_loop_env(scev, cur_header, idx), sizeof(*cur_env));
		if (nested_loop) {
			/*
			 * Process nested loop as a single instruction by
			 * forgetting anything non-invariant in the nested loop
			 */
			if (log_level2)
				memcpy(old_env, cur_env, sizeof(*old_env));
			/* Pull the nested loop's stack-store base dependencies up. */
			for_each_set_bit(r, nested_loop->store_base_regs, BPF_SCEV_REGS_NUM)
				or_expr_regs(env, cur_env->reg2expr[r], cur_loop->store_base_regs);
			forget_non_invariants(env, cur_env, idx);
			if (log_level2)
				log_env_changes(env, LOG_AT_TRANSFER, old_env, cur_env, idx);
			/*
			 * Treat nested loop exits as successors,
			 * join cur_env into successor's envs.
			 */
			for (i = 0; i < nested_loop->exits_cnt; i++) {
				s = nested_loop->exits[i].to;
				err = join_successor(env, cur_header, s, old_env, cur_env);
				if (err)
					goto out;
			}
		} else {
			/*
			 * Iterate instructions within a single basic block
			 * starting at 'idx' mutating 'cur_env'.
			 */
			for (;;) {
				if (log_level2)
					memcpy(old_env, cur_env, sizeof(*old_env));
				collect_store_base_regs(env, cur_env, idx, cur_loop->store_base_regs);
				err = transfer(env, cur_env, idx);
				if (err)
					goto out;
				reset_scevs_at_indirect_writes(env, cur_env, idx);
				if (log_level2)
					log_env_changes(env, LOG_AT_TRANSFER, old_env, cur_env, idx);
				succ = bpf_insn_successors(env, idx);
				if (succ->cnt != 1)
					break;
				succ_idx = succ->items[0];
				if (aux[idx].bb_end || aux[succ_idx].need_scev ||
				    bpf_loop_at_index(env, succ_idx) != cur_header)
					break;
				idx = succ_idx;
			}
			/* Join cur_env into basic block successor's envs. */
			iarray_for_each(s, succ) {
				err = join_successor(env, cur_header, s, old_env, cur_env);
				if (err)
					goto out;
			}
		}
	}

	err = 0;
out:
	kfree(cur_env);
	kfree(old_env);
	return err;
nomem:
	err = -ENOMEM;
	goto out;
}

static void mark_latches(struct bpf_verifier_env *env)
{
	struct bpf_insn_aux_data *aux = env->insn_aux_data;
	struct bpf_loop *loop;
	int len = env->prog->len;
	int i, j, k, from;

	for (i = 0; i < len; i++) {
		loop = aux[i].loop;
		if (!loop)
			continue;
		aux[i].need_scev = true;
		if (!can_compute_loop_scev(loop))
			continue;
		/*
		 * Make sure to only set .need_scev for exits and latches
		 * belonging to the loop at `i`. If can_compute_loop_scev()
		 * fails for a loop, it should hide it's instructions from
		 * compute_insn_scevs().
		 */
		for (j = 0; j < loop->backedges_cnt; j++)
			for (k = 0; k < loop->backedges[j].latches_cnt; k++)
				aux[loop->backedges[j].latches[k]].need_scev = true;
		for (j = 0; j < loop->exits_cnt; j++) {
			from = loop->exits[j].from;
			if (bpf_loop_at_index(env, from) == i)
				aux[from].need_scev = true;
		}
	}
}

static bool is_any_imm_reg_opaque(struct scev *scev, u32 id)
{
	u32 l, r, reg, order;
	s64 imm;

	scev->stack_sz = 0;
	expr_stack_push(scev, id);
	while (expr_next(scev, &id, &order)) {
		if (order & DEPTH_LIMIT)
			return false;
		if (!(order & PRE) ||
		    is_imm(scev, id, &imm) ||
		    is_reg(scev, id, &reg) ||
		    is_opaque(scev, id) ||
		    is_any(scev, id, &l, &r))
			continue;
		return false;
	}
	return true;
}

/*
 * Can implement explicit stack version, but it is harder to read.
 * Stick with recursive version for now.
 */
static int transform_expr_once(struct scev *scev, u32 lvl, u32 root, void *priv,
			       int (*fn)(struct scev *scev, u32 id, void *priv))
{
	struct expr expr;
	int p0, p1, id;

	if (lvl >= EXPR_STACK_DEPTH)
		return UNKNOWN_EXPR_ID;

	expr = scev->exprs[root]; /* snapshot the expr before potential realloc */
	switch (op_params_num(expr.op)) {
	case 0:
		id = root;
		break;
	case 1:
		p0 = transform_expr_once(scev, lvl + 1, expr.params[0], priv, fn);
		id = expr1(scev, expr.op, p0);
		break;
	case 2:
		p0 = transform_expr_once(scev, lvl + 1, expr.params[0], priv, fn);
		p1 = transform_expr_once(scev, lvl + 1, expr.params[1], priv, fn);
		id = expr2(scev, expr.op, p0, p1);
		break;
	}
	return id < 0 ? id : fn(scev, id, priv);
}

static int transform_expr(struct scev *scev, u32 root, void *priv,
			  int (*fn)(struct scev *scev, u32 id, void *priv))
{
	int i = 0, id_old, id_new = root;

	do {
		/*
		 * The depth limit is somewhat arbitrary and is essentially a failsafe.
		 * oscillation should not really happen. Still, it seems harsh to error out.
		 */
		if (i++ == 4 * EXPR_STACK_DEPTH)
			return UNKNOWN_EXPR_ID;
		id_old = id_new;
		id_new = transform_expr_once(scev, 0, id_old, priv, fn);
		if (id_new < 0)
			return id_new;
	} while (id_old != id_new);
	return id_new;
}

static int simplify(struct scev *scev, u32 id, void *priv)
{
	int a, b, l, r, t, base, slope;
	s64 imm1, imm2;

	/* (+ (linear base slope) imm) -> (linear (+ base imm) slope) */
	if (is_add(scev, id, &l, &r) &&
	    is_linear(scev, l, &base, &slope) &&
	    is_imm(scev, r, &imm1))
		return expr2(scev, LINEAR_SCEV, expr2(scev, BPF_ADD, base, r), slope);

	/* (+ imm imm) -> imm */
	if (is_add(scev, id, &l, &r) &&
	    is_imm(scev, l, &imm1) &&
	    is_imm(scev, r, &imm2))
		return imm_expr(scev, imm1 + imm2);

	/* (zext32 (imm X)) -> (imm (u32)X) */
	if (is_zext32(scev, id, &l) && is_imm(scev, l, &imm1))
		return imm_expr(scev, (u64)(u32)imm1);

	/*
	 * (zext32 (+ (zext32 $a) $b)) -> (zext32 (+ $a $b))
	 * (zext32 (+ $a (zext32 $b))) -> (zext32 (+ $a $b))
	 */
	if ((is_zext32(scev, id, &t) && is_add(scev, t, &l, &b) && is_zext32(scev, l, &a)) ||
	    (is_zext32(scev, id, &t) && is_add(scev, t, &a, &r) && is_zext32(scev, r, &b))) {
		t = expr2(scev, BPF_ADD, a, b);
		t = expr1(scev, ZEXT32, t);
		return t;
	}

	return id;
}

static int compute_header_scevs(struct bpf_verifier_env *env, struct env *header_env)
{
	struct scev *scev = env->scev;
	int l, r, t, id, ra, rb, ra_expr;
	s64 imm;

	/*
	 * Note:
	 * reg2expr[ra] describes a change in rA's value over a single loop iteration.
	 * It does not account for rA's entry value. However, the SCEV constructed for rA
	 * must include an additional rA entry edge. For example:
	 *
	 *      r0 = ..., r1 = 7
	 *   l: if r0 == 10 goto e // reg2expr[r1] == (imm 42) => reg2scev[r1] == (any r1 42)
	 *      r0 += 1
	 *      r1 = 42
	 *      goto l
	 *   e:
	 *      ... at exit r1 is in {7,42} ...
	 */
	for (ra = 0; ra < REGS_NUM; ra++) {
		ra_expr = transform_expr(scev, header_env->reg2expr[ra], NULL, simplify);
		if (ra_expr < 0)
			return ra_expr;
		header_env->reg2expr[ra] = ra_expr;
		/* rA = (+ rA IMM) -> (linear rA IMM) */
		if (is_add(scev, ra_expr, &l, &r) &&
		    is_reg(scev, l, &rb) &&
		    is_imm(scev, r, &imm) &&
		    ra == rb) {
			id = expr2(scev, LINEAR_SCEV, l, r);
			if (id < 0)
				return id;
			header_env->reg2scev[ra] = id;
			continue;
		}
		/* rA = (is_zext32 (+ rA IMM)) -> (is_zext32 (linear rA IMM)) */
		if (is_zext32(scev, ra_expr, &t) &&
		    is_add(scev, t, &l, &r) &&
		    is_reg(scev, l, &rb) &&
		    is_imm(scev, r, &imm) &&
		    ra == rb) {
			id = expr2(scev, LINEAR_SCEV, l, r);
			id = expr1(scev, ZEXT32, id);
			if (id < 0)
				return id;
			header_env->reg2scev[ra] = id;
			continue;
		}
		/* rA = rA -> rA*/
		if (is_reg(scev, ra_expr, &rb) && ra == rb) {
			header_env->reg2scev[ra] = ra_expr;
			continue;
		}
		/* rA = (any 1 2 3 4 ...) -> (any rA 1 2 3 4 ...) */
		if (is_any_imm_reg_opaque(scev, ra_expr)) {
			id = mk_any(scev, expr1(scev, REG, ra), ra_expr);
			if (id < 0)
				return id;
			header_env->reg2scev[ra] = id;
			continue;
		}

	}
	return 0;
}
static int instantiate_header_scevs(struct scev *scev, u32 id, void *priv)
{
	u32 reg, *reg2scev = priv;

	/* (reg r) -> ... equation at header ... */
	if (is_reg(scev, id, &reg))
		return reg2scev[reg];
	/* keep expression shapes */
	return id;
}

static int compute_insn_scevs(struct bpf_verifier_env *env, struct env *eheader, struct env *einsn)
{
	struct scev *scev = env->scev;
	int id, reg;

	for (reg = 0; reg < REGS_NUM; reg++) {
		id = einsn->reg2expr[reg];
		id = transform_expr_once(scev, 0, id, eheader->reg2scev, instantiate_header_scevs);
		if (id < 0)
			return id;
		id = transform_expr(scev, id, NULL, simplify);
		if (id < 0)
			return id;
		einsn->reg2scev[reg] = id;
	}

	return 0;
}

static int log_scevs(struct bpf_verifier_env *env)
{
	struct bpf_insn_aux_data *aux = env->insn_aux_data;
	struct bpf_verifier_log *log = &env->log;
	struct env *header_env, *latch_env;
	struct scev *scev = env->scev;
	struct bpf_loop *loop;
	int i, j, k, len, latch;

	len = env->prog->len;
	for (i = 0; i < len; i++) {
		loop = aux[i].loop;
		if (!loop)
			continue;
		header_env = find_header_env(scev, i);
		bpf_log(log, "scev at header %d:\n", i);
		print_env(env, header_env, i);
		if (!can_compute_loop_scev(loop))
			continue;
		for (j = 0; j < loop->backedges_cnt; j++) {
			for (k = 0; k < loop->backedges[j].latches_cnt; k++) {
				latch = loop->backedges[j].latches[k];
				bpf_log(log, " scev at %s %d:\n", k ? "exit" : "latch", latch);
				latch_env = find_loop_env(scev, i, latch);
				if (verifier_bug_if(!latch_env, env, "i=%d, latch=%d", i, latch))
					return -EFAULT;
				print_env(env, latch_env, latch);
			}
		}
	}
	return 0;
}

int bpf_compute_scev(struct bpf_verifier_env *env)
{
	struct bpf_insn_aux_data *aux = env->insn_aux_data;
	struct scev *scev = env->scev;
	int *postorder = env->cfg.insn_postorder;
	int cnt = env->cfg.cur_postorder;
	int i, idx, err, header;

	mark_latches(env);
	/*
	 * Visit loop headers in postorder, to guarantee that scevs
	 * for innermost loops are computed first.
	 */
	for (i = 0; i < cnt; i++) {
		idx = postorder[i];
		if (!aux[idx].loop)
			continue;
		err = compute_scev_for_loop(env, idx);
		if (err)
			return err;
	}

	/*
	 * Compute scevs from exprs collected on a previous step. Iterate instructions in
	 * reverse post-order so that each loop header is processed before instructions
	 * reachable from it.
	 */
	for (i = cnt - 1; i >= 0; i--) {
		idx = postorder[i];
		if (!aux[idx].need_scev)
			continue;
		header = bpf_loop_at_index(env, idx);
		err = aux[idx].loop
		      ? compute_header_scevs(env, find_header_env(scev, header))
		      : compute_insn_scevs(env,
					   find_header_env(scev, header),
					   find_loop_env(scev, header, idx));
		if (err)
			return err;
	}

	for (i = 0; i < env->prog->len; i++)
		if (aux[i].loop)
			aux[i].prune_point = true;

	if (env->log.level & BPF_LOG_LEVEL2) {
		err = log_scevs(env);
		if (err)
			return err;
	}

	return 0;
}

void bpf_free_scev(struct bpf_verifier_env *env)
{
	struct scev *scev = env->scev;
	struct insn_envs *envs;
	int i;
	u32 j;

	if (!scev)
		return;
	for (i = 0; i < scev->envs_cnt; i++) {
		envs = scev->envs[i];
		if (!envs)
			continue;
		for (j = 0; j < envs->cnt; j++)
			kfree(envs->entries[j].env);
		kfree(envs);
	}
	if (scev->exprs_ht)
		for (i = 0; i < scev->exprs_ht_cnt; i++)
			kvfree(scev->exprs_ht[i]);
	kvfree(scev->exprs_ht);
	kvfree(scev->worklist.data);
	kvfree(scev->envs);
	kvfree(scev->exprs);
	kvfree(scev->discovered);
	kfree(scev);
	env->scev = NULL;
}

int bpf_init_scev(struct bpf_verifier_env *env)
{
	struct scev *scev;

	scev = kzalloc(sizeof(struct scev), GFP_KERNEL_ACCOUNT);
	if (!scev)
		return -ENOMEM;
	env->scev = scev;
	bpf_mark_reg_known_scalar(&scev->zero_reg, 0);
	bpf_mark_reg_unknown_imprecise(&scev->unknown_reg);
	/* A power of two for hash & (exprs_ht_cnt - 1)` indexing. */
	scev->exprs_ht_cnt = roundup_pow_of_two(max(256U, DIV_ROUND_UP(env->prog->len, 4)));
	scev->exprs_ht = kvcalloc(scev->exprs_ht_cnt, sizeof(*scev->exprs_ht), GFP_KERNEL_ACCOUNT);
	if (!scev->exprs_ht)
		goto nomem;
	if (expr0(scev, UNKNOWN) < 0 || expr0(scev, OPAQUE)  < 0)
		goto nomem;
	scev->envs = kvcalloc(env->prog->len, sizeof(*scev->envs), GFP_KERNEL_ACCOUNT);
	scev->discovered = kvcalloc(env->prog->len, sizeof(*scev->discovered), GFP_KERNEL_ACCOUNT);
	if (!scev->envs || !scev->discovered)
		goto nomem;
	/*
	 * Remember original program length, in case bpf_free_scev()
	 * is called after bpf program rewrites that increase program
	 * length.
	 */
	scev->envs_cnt = env->prog->len;
	return 0;
nomem:
	bpf_free_scev(env);
	return -ENOMEM;
}

static struct bpf_stack_state *scev_regno_to_stack(const struct bpf_func_state *st, u32 r)
{
	int spi, slots_available;

	if (r < MAX_BPF_REG)
		return NULL;

	slots_available = st->allocated_stack / BPF_REG_SIZE;
	spi = r - MAX_BPF_REG;
	if (spi < slots_available)
		return &st->stack[spi];

	return NULL;
}

static struct bpf_reg_state *scev_regno_to_reg(struct bpf_func_state *st, u32 r)
{
	return r < MAX_BPF_REG ? &st->regs[r] : NULL;
}

/* Linear widening requires a physical register or a full spill. */
static struct bpf_reg_state *scev_reg_for_linear(struct bpf_func_state *st, u32 r)
{
	struct bpf_stack_state *stack;

	if (r < MAX_BPF_REG)
		return scev_regno_to_reg(st, r);
	stack = scev_regno_to_stack(st, r);
	if (!stack || bpf_spill_size(stack) != BPF_REG_SIZE)
		return NULL;
	return &stack->spilled_ptr;
}

/*
 * Return a borrowed value; never use it as a widening destination.
 * NULL denotes no readable value.
 */
static const struct bpf_reg_state *
scev_reg_for_read(struct bpf_verifier_env *env, const struct bpf_func_state *st, u32 r)
{
	const struct bpf_stack_state *stack;
	bool zero = true;
	int i;

	if (r < MAX_BPF_REG)
		return st->regs[r].type == NOT_INIT ? NULL : &st->regs[r];
	if (r >= REGS_NUM || r - MAX_BPF_REG >= env->stack_limit / BPF_REG_SIZE)
		return NULL;

	stack = scev_regno_to_stack(st, r);
	if (!stack)
		return &env->scev->unknown_reg;
	if (bpf_spill_size(stack) == BPF_REG_SIZE)
		return &stack->spilled_ptr;

	/* Partial spills do not describe the value of the whole slot. */
	for (i = 0; i < BPF_REG_SIZE; i++) {
		switch (stack->slot_type[i]) {
		case STACK_ZERO:
			break;
		case STACK_INVALID:
		case STACK_POISON:
		case STACK_MISC:
			zero = false;
			break;
		case STACK_SPILL:
			if (!bpf_is_spilled_scalar_reg(stack))
				return NULL;
			zero = false;
			break;
		default:
			return NULL;
		}
	}
	return zero ? &env->scev->zero_reg : &env->scev->unknown_reg;
}

/* Materialize a writable value in the current state, growing its stack if needed. */
static int scev_reg_for_write(struct bpf_verifier_env *env, struct bpf_func_state *st,
			      u32 r, struct bpf_reg_state **out)
{
	const struct bpf_reg_state *value;
	struct bpf_stack_state *stack;
	u32 spi;
	int err;

	*out = scev_regno_to_reg(st, r);
	if (*out)
		return 0;
	if (r >= REGS_NUM || r - MAX_BPF_REG >= env->stack_limit / BPF_REG_SIZE)
		return -EINVAL;

	spi = r - MAX_BPF_REG;
	err = bpf_grow_stack_state(env, st, (spi + 1) * BPF_REG_SIZE);
	if (err)
		return err;

	/* Resolve pointers after growth, which may relocate the stack. */
	value = scev_reg_for_read(env, st, r);
	if (!value)
		return -EINVAL;
	stack = scev_regno_to_stack(st, r);
	stack->spilled_ptr = *value;
	memset(stack->slot_type, STACK_SPILL, sizeof(stack->slot_type));
	*out = &stack->spilled_ptr;
	return 0;
}

static bool scev_reg_alive(struct bpf_verifier_env *env, struct bpf_verifier_state *st, u32 r)
{
	int insn_idx = bpf_frame_insn_idx(st, st->curframe);
	u16 live_regs = env->insn_aux_data[insn_idx].live_regs_before;
	int spi;

	if (r < MAX_BPF_REG) {
		return BIT(r) & live_regs;
	} else {
		spi = r - MAX_BPF_REG;
		return bpf_stack_slot_alive(env, st->curframe, spi * 2) ||
		       bpf_stack_slot_alive(env, st->curframe, spi * 2 + 1);
	}
}

/*
 * Latch is a condition deciding if execution remains inside a loop.
 * Linear latch represents a condition 'if <reg> <op> <loop invariant> goto <loop-header>',
 * where equation '<base> + i * <step> <op> <bound>' describes values taken by register <reg>,
 * 'i' is the loop iteration number, starting from 0.
 * 'zext32' indicates the latch is for (zext32 (linear ...)) SCEV.
 * 'jmp32' indicates that the latch is a JMP32 instruction.
 */
struct linear_latch {
	u32 insn_idx;
	u32 base_expr;
	u32 step_expr;
	u32 bound_expr;
	u8 op;
	bool zext32;
	bool jmp32;
};

static bool loop_invariant(struct scev *scev, u32 id)
{
	s64 imm;
	u32 reg;

	return is_reg(scev, id, &reg) || is_imm(scev, id, &imm);
}

static int match_linear_latch(struct bpf_verifier_env *env,
			      u32 header,
			      u32 latch_idx,
			      struct linear_latch *latch)
{
	struct bpf_insn *insn = &env->prog->insnsi[latch_idx];
	struct scev *scev = env->scev;
	struct env *latch_env;
	u32 true_branch_loop;
	u32 true_branch_tgt;
	u32 src_reg_scev;
	u32 dst_reg_scev;
	u32 t, l, r, op;
	int id;

	op = BPF_OP(insn->code);
	latch->jmp32 = BPF_CLASS(insn->code) == BPF_JMP32;
	/* Flip the condition if true branch jumps out of the loop. */
	true_branch_tgt = latch_idx + bpf_jmp_offset(insn) + 1;
	true_branch_loop = bpf_loop_at_index(env, true_branch_tgt);
	if (true_branch_loop != header &&
	    !bpf_is_nested_loop(env, true_branch_loop, header))
		op = bpf_rev_opcode(op);
	switch (op) {
	case BPF_JSLT:
	case BPF_JSLE:
	case BPF_JSGT:
	case BPF_JSGE:
	case BPF_JLT:
	case BPF_JLE:
	case BPF_JGT:
	case BPF_JGE:
	case BPF_JNE:
		break;
	default:
		return false;
	}

	latch->insn_idx = latch_idx;

	latch_env = find_loop_env(scev, header, latch_idx);
	if (verifier_bug_if(!latch_env, env, "header_idx=%d, latch_idx=%d\n", header, latch_idx))
		return -EFAULT;
	dst_reg_scev = latch_env->reg2scev[insn->dst_reg];
	src_reg_scev = latch_env->reg2scev[insn->src_reg];
	/*
	 * Make sure (linear ...) is in dst_reg_scev:
	 *   'if <invariant> <op> <linear>' ⇔
	 * 'if <linear> <flip(op)> <invariant>'.
	 */
	if (BPF_SRC(insn->code) == BPF_X &&
	    is_linear_or_linear32(scev, src_reg_scev, &l, &r, &latch->zext32)) {
		swap(dst_reg_scev, src_reg_scev);
		op = bpf_flip_opcode(op);
	}
	latch->op = op;
	/*
	 * simplify() produces two shapes for linear latches:
	 * - (linear register step)
	 *           ^----------------------------+-- base_expr
	 *           base_expr                    |
	 * - (linear (+ register imm) step)       |
	 *           ^----------------------------'
	 */
	if (!is_linear_or_linear32(scev, dst_reg_scev,
				   &latch->base_expr, &latch->step_expr, &latch->zext32))
		return false;

	if (BPF_SRC(insn->code) == BPF_K) {
		id = imm_expr(scev, insn->imm);
		if (id < 0)
			return id;
		latch->bound_expr = id;
	} else {
		latch->bound_expr = src_reg_scev;
	}

	if (!loop_invariant(scev, latch->step_expr) ||
	    !loop_invariant(scev, latch->bound_expr))
		return false;

	/* (linear register step) */
	if (is_reg(scev, latch->base_expr, &t))
		return true;

	/* (linear (+ register imm) step) */
	if (is_add(scev, latch->base_expr, &l, &r) &&
	    is_reg(scev, l, &t) &&
	    loop_invariant(scev, r))
		return true;

	return false;
}

struct scev_value {
	u64 value;	/* scalar value or a pointer offset */
	int ptr_reg;	/* SCEV register identifying the pointer origin, or -1. */
};

static void log_scev_value(struct bpf_verifier_env *env, struct bpf_func_state *st,
			   const struct scev_value *v)
{
	const struct bpf_reg_state *reg;

	if (v->ptr_reg >= 0) {
		reg = scev_reg_for_read(env, st, v->ptr_reg);
		log_reg(env, v->ptr_reg);
		bpf_log(&env->log, " (%s)", reg ? reg_type_str(env, reg->type) : "unavailable");
	} else {
		bpf_log(&env->log, "scalar %llu", v->value);
	}
}

static void log_incompatible_scev_values(struct bpf_verifier_env *env, const char *reason,
					struct bpf_func_state *st,
					const struct scev_value *a, const struct scev_value *b)
{
	bpf_log(&env->log, "scev: %s ", reason);
	log_scev_value(env, st, a);
	bpf_log(&env->log, ", ");
	log_scev_value(env, st, b);
	bpf_log(&env->log, "\n");
}

static bool scev_value_compatible(struct bpf_verifier_env *env, struct bpf_func_state *st,
				  const struct scev_value *a, const struct scev_value *b)
{
	const struct bpf_reg_state *ra, *rb;

	if (a->ptr_reg < 0 || b->ptr_reg < 0) {
		if (a->ptr_reg < 0 && b->ptr_reg < 0)
			return true;
		goto incompatible;
	}
	ra = scev_reg_for_read(env, st, a->ptr_reg);
	rb = scev_reg_for_read(env, st, b->ptr_reg);
	if (ra && rb && bpf_same_memory_origin(ra, rb))
		return true;

incompatible:
	if (env->log.level & BPF_LOG_LEVEL2)
		log_incompatible_scev_values(env, "incompatible latch operands", st, a, b);
	return false;
}

static bool scev_value_add(struct bpf_verifier_env *env, struct bpf_func_state *st,
			   const struct scev_value *a, const struct scev_value *b,
			   struct scev_value *result)
{
	if (a->ptr_reg >= 0 && b->ptr_reg >= 0) {
		if (env->log.level & BPF_LOG_LEVEL2)
			log_incompatible_scev_values(env, "can't add two pointers", st, a, b);
		return false;
	}
	result->value = a->value + b->value;
	result->ptr_reg = a->ptr_reg >= 0 ? a->ptr_reg : b->ptr_reg;
	return true;
}

static bool eval_expr(struct bpf_verifier_env *env, struct scev *scev,
		      struct bpf_func_state *st, u32 id, struct scev_value *result)
{
	struct scev_value lval, rval;
	const struct bpf_reg_state *reg;
	u32 l, r, regno;
	s64 imm;

	if (is_reg(scev, id, &regno)) {
		reg = scev_reg_for_read(env, st, regno);
		if (!reg || reg->type == NOT_INIT || !tnum_is_const(reg->var_off))
			return false;
		result->value = reg->var_off.value;
		result->ptr_reg = reg->type == SCALAR_VALUE ? -1 : (int)regno;
		return true;
	} else if (is_imm(scev, id, &imm)) {
		result->value = imm;
		result->ptr_reg = -1;
		return true;
	} else if (is_add(scev, id, &l, &r) &&
		   eval_expr(env, scev, st, l, &lval) &&
		   eval_expr(env, scev, st, r, &rval)) {
		return scev_value_add(env, st, &lval, &rval, result);
	}
	return false;
}

static bool is_signed_op(u8 op)
{
	switch (op) {
	case BPF_JSLT:
	case BPF_JSLE:
	case BPF_JSGT:
	case BPF_JSGE:
		return true;
	default:
		return false;
	}
}

static bool compute_backedge_taken_count(u8 op, u64 initial, u64 step, u64 bound, u64 *result)
{
	u64 rem, diff, backedge_taken_count;
	bool up, down;

	if (step == 0)
		return false;
	down = (s64)step < 0;
	up = (s64)step > 0;
	diff = bound - initial;
	switch (op) {
	case BPF_JLT:
		/*
		 * E.g.: 3 + 2*i < 7 ⇔ 2*i < 4 ⇔ i ∈ [0,1].
		 * Here the inequality describes a set of values `i` might take
		 * while the latch condition remains true (the backedge is taken).
		 * The backedge_taken_count is the size of this set.
		 */
		if (/*
		     * If the condition is initially true, decreasing values won't
		     * change that without underflow. If the condition is initially false,
		     * the main pass would do a single iteration anyway.
		     */
		    down ||
		    /* Is the latch condition false on the first iteration? */
		    initial >= bound ||
		    /* Count iterations satisfying the strict inequality, including i=0. */
		    check_add_overflow(div64_u64(diff - 1, step), 1, &backedge_taken_count) ||
		    /*
		     * Check if the last iteration overflows the counter:
		     *   initial + backedge_taken_count * step > U64_MAX
		     */
		    backedge_taken_count > div64_u64(U64_MAX - initial, step))
			return false;
		break;
	case BPF_JLE:
		/* E.g.: 3 + 2*i ≤ 7 ⇔ 2*i ≤ 4 ⇔ i ∈ [0,1,2]. */
		if (down || initial > bound ||
		    /* Count iterations satisfying the non-strict inequality. */
		    check_add_overflow(div64_u64(diff, step), 1, &backedge_taken_count) ||
		    backedge_taken_count > div64_u64(U64_MAX - initial, step))
			return false;
		break;
	case BPF_JGT:
		/* E.g.: 7 - 2*i > 3 ⇔ 2*i < 4 ⇔ i ∈ [0,1]. */
		if (up || initial <= bound ||
		    check_add_overflow(div64_u64(-diff - 1, -step), 1, &backedge_taken_count) ||
		    /*
		     * The last iteration underflows the counter if:
		     *   initial + backedge_taken_count * step < 0 ⇔
		     *             backedge_taken_count * step < -initial ⇔
		     *                    backedge_taken_count > initial / -step
		     * (direction flips because step < 0)
		     */
		    backedge_taken_count > div64_u64(initial, -step))
			return false;
		break;
	case BPF_JGE:
		/* E.g.: 7 - 2*i ≥ 3 ⇔ 2*i ≤ 4 ⇔ i ∈ [0,1,2]. */
		if (up || initial < bound ||
		    check_add_overflow(div64_u64(-diff, -step), 1, &backedge_taken_count) ||
		    backedge_taken_count > div64_u64(initial, -step))
			return false;
		break;
	case BPF_JSLT:
		if (down || (s64)initial >= (s64)bound ||
		    check_add_overflow(div64_u64(diff - 1, step), 1, &backedge_taken_count) ||
		    /*
		     * Check if the last iteration overflows the signed counter:
		     *   (s64)initial + backedge_taken_count * step > S64_MAX
		     */
		    backedge_taken_count > div64_u64((u64)S64_MAX - initial, step))
			return false;
		break;
	case BPF_JSLE:
		if (down || (s64)initial > (s64)bound ||
		    check_add_overflow(div64_u64(diff, step), 1, &backedge_taken_count) ||
		    backedge_taken_count > div64_u64((u64)S64_MAX - initial, step))
			return false;
		break;
	case BPF_JSGT:
		if (up || (s64)initial <= (s64)bound ||
		    check_add_overflow(div64_u64(-diff - 1, -step), 1, &backedge_taken_count) ||
		    /*
		     * The last iteration underflows the signed counter if:
		     *   (s64)initial + backedge_taken_count * step < S64_MIN ⇔
		     *                  backedge_taken_count * step < S64_MIN - (s64)initial ⇔
		     *                         backedge_taken_count > initial - (u64)S64_MIN / -step
		     *	 (direction flips because step < 0)
		     */
		    backedge_taken_count > div64_u64(initial - (u64)S64_MIN, -step))
			return false;
		break;
	case BPF_JSGE:
		if (up || (s64)initial < (s64)bound ||
		    check_add_overflow(div64_u64(-diff, -step), 1, &backedge_taken_count) ||
		    backedge_taken_count > div64_u64(initial - (u64)S64_MIN, -step))
			return false;
		break;
	case BPF_JNE:
		/*
		 * For a decreasing counter flip the diff and step:
		 *   7 - 2*i ≠ 3 ⇔ 2*i ≠ 7 - 3.
		 */
		diff = down ? -diff : diff;
		step = down ? -step : step;
		backedge_taken_count = div64_u64_rem(diff, step, &rem);
		if (diff == 0 || rem != 0)
			return false;
		break;
	default:
		return false;
	}

	*result = backedge_taken_count;
	return true;
}

/*
 * Loop with post-condition:
 *
 *    r0 = 0
 * l: ...                r0 ∈ [0,1,2] header executed 3 times
 *    r0 += 1            r0 ∈ [0,1,2]
 *    ...                r0 ∈ [1,2,3]
 *    if r0 != 3 goto l  r0 ∈ [1,2,3] backedge taken 2 times
 *    ...                r0 ∈ [3]
 *
 * SCEV at header: r0 = k
 * SCEV at latch:  r0 = 1 + k
 *
 * Loop with pre-condition:
 *
 *    r0 = 0
 * l: ...                r0 ∈ [0,1,2,3] header executed 4 times
 *    if r0 == 3 goto e  r0 ∈ [0,1,2,3] backedge taken 3 times
 *    r0 += 1            r0 ∈ [0,1,2]
 *    ...                r0 ∈ [1,2,3]
 *    goto l             r0 ∈ [1,2,3]
 * e: ...                r0 ∈ [3]
 *
 * SCEV at header: r0 = k
 * SCEV at latch:  r0 = k
 */
static bool compute_max_iters(struct bpf_verifier_env *env,
			      struct bpf_func_state *st,
			      struct linear_latch *latch,
			      struct bpf_loop_iters *iters)
{
	struct bpf_insn_aux_data *aux = env->insn_aux_data;
	struct bpf_loop *loop = aux[bpf_loop_at_index(env, latch->insn_idx)].loop;
	struct scev *scev = env->scev;
	struct scev_value initial_val, step_val, bound_val;
	u64 step, bound, initial, total_change, backedge_taken_count;
	u8 op = latch->op;

	if (!eval_expr(env, scev, st, latch->base_expr, &initial_val) ||
	    !eval_expr(env, scev, st, latch->step_expr, &step_val) ||
	    !eval_expr(env, scev, st, latch->bound_expr, &bound_val))
		return false;

	if (step_val.ptr_reg >= 0) {
		if (env->log.level & BPF_LOG_LEVEL2) {
			bpf_log(&env->log, "scev: latch step is a pointer ");
			log_scev_value(env, st, &step_val);
			bpf_log(&env->log, "\n");
		}
		return false;
	}
	if (!scev_value_compatible(env, st, &initial_val, &bound_val))
		return false;

	initial = initial_val.value;
	step = step_val.value;
	bound = bound_val.value;

	/*
	 * The code below only handles mixed 64-bit/32-bit comparisons/recurrences
	 * when initial bounds and step fit in 32-bit domain.
	 */
	if (latch->zext32 || latch->jmp32) {
		/*
		 * With the above constraint doing `l: w0 += 1; if r0 s< 256 goto l;`
		 *                is equivalent to `l: w0 += 1; if r0 < 256 goto l;`
		 */
		if (latch->jmp32 && is_signed_op(op)) {
			initial = (s64)(s32)(u32)initial;
			bound   = (s64)(s32)(u32)bound;
			step    = (s64)(s32)(u32)step;
		} else {
			initial = (u32)initial;
			bound   = (u32)bound;
			step    = (s64)(s32)(u32)step;
		}
	}
	if (latch->zext32 ^ latch->jmp32 && (initial != initial_val.value ||
					     bound != bound_val.value ||
					     step != step_val.value)) {
		if (env->log.level & BPF_LOG_LEVEL2)
			bpf_log(&env->log,
				"scev: mixed-width latch has operands outside 32-bit ranges\n");
		return false;
	}


	if (!compute_backedge_taken_count(op, initial, step, bound, &backedge_taken_count))
		return false;

	if (latch->zext32 || latch->jmp32) {
		/*
		 * compute_backedge_taken_count() guarantees that 'total_change'
		 * does not overflow in 64-bit domain.
		 */
		total_change = initial + step * backedge_taken_count;
		if (latch->jmp32 && is_signed_op(op)) {
			if ((s64)total_change < S32_MIN || (s64)total_change > S32_MAX)
				return false;
		} else {
			if (total_change > U32_MAX)
				return false;
		}
	}

	/*
	 * max_header_count is u32 and U32_MAX means "infinite",
	 * check to avoid overflow below.
	 */
	if (backedge_taken_count >= U32_MAX - 1)
		return false;

	/* The header executes once on entry and once for each taken backedge, hence +1. */
	iters->max_header_count = backedge_taken_count + 1;
	/*
	 * The latch is a conditional jump with one jump target exiting the loop.
	 * Linear latch is matched only if the loop has a single backedge.
	 * The loop still, however can have multiple exits.
	 * In such case, conservatively assume that non-latch exit can happen
	 * at any iteration, thus setting minimal number of iterations as 0.
	 */
	iters->min_header_count = loop->exits_cnt == 1 ? iters->max_header_count : 0;
	return true;
}

static void mark_scev_reg_scratched(struct bpf_verifier_env *env, u32 r)
{
	if (r < MAX_BPF_REG)
		mark_reg_scratched(env, r);
	else
		mark_stack_slot_scratched(env, r - __MAX_BPF_REG);
}

/*
 * An unknown scalar is its own widening.
 * Stack bytes without a value are represented by scev->unknown_reg.
 */
static bool is_unbound_scev_reg(struct bpf_verifier_env *env, const struct bpf_func_state *st,
				u32 r)
{
	const struct bpf_reg_state *reg = scev_reg_for_read(env, st, r);

	return reg && reg->type == SCALAR_VALUE && !reg->id && reg->step <= 1 &&
	       tnum_is_unknown(reg->var_off) &&
	       reg->r64.size == U64_MAX && reg->r32.size == U32_MAX;
}

/* Main logic in verifier.c forbids varying offsets for certain register types. */
static bool is_widenable_reg_type(const struct bpf_reg_state *reg)
{
	if (type_may_be_null(reg->type))
		return false;

	switch (base_type(reg->type)) {
	case SCALAR_VALUE:
	case PTR_TO_MAP_VALUE:
	case PTR_TO_MAP_KEY:
	case PTR_TO_STACK:
	case PTR_TO_PACKET:
	case PTR_TO_PACKET_META:
	case PTR_TO_MEM:
	case PTR_TO_BUF:
	case PTR_TO_BTF_ID:
		return true;
	default:
		return false;
	}
}

static void scratch_widened_reg_id(struct bpf_verifier_env *env, struct bpf_reg_state *reg)
{
	switch (base_type(reg->type)) {
	case SCALAR_VALUE:
		reg->id = 0;
		break;
	case PTR_TO_PACKET:
	case PTR_TO_PACKET_META:
		reg->id = ++env->id_gen;
		break;
	default:
		break;
	}
}

/* Check that ANY leaves can be unioned with r's loop-entry value. */
static bool is_widenable_any(struct bpf_verifier_env *env, struct bpf_func_state *loop_entry,
			     struct env *header_env, u32 r, u32 id)
{
	const struct bpf_reg_state *reg, *leaf_reg;
	struct scev *scev = env->scev;
	u32 l, ra, rr, leaf, order;
	s64 imm;

	if (!is_any(scev, id, &l, &rr))
		return false;
	reg = scev_reg_for_read(env, loop_entry, r);
	if (!reg || !is_widenable_reg_type(reg))
		return false;

	scev->stack_sz = 0;
	expr_stack_push(scev, id);
	while (expr_next(scev, &id, &order)) {
		if (order & DEPTH_LIMIT)
			return false;
		if (!(order & PRE) || is_any(scev, id, &l, &rr))
			continue;
		if (is_imm(scev, id, &imm)) {
			if (reg->type != SCALAR_VALUE)
				return false;
		} else if (is_reg(scev, id, &leaf)) {
			/* Other leaves must denote loop-invariant registers. */
			if (leaf != r &&
			    !(is_reg(scev, header_env->reg2scev[leaf], &ra) && ra == leaf))
				return false;
			leaf_reg = scev_reg_for_read(env, loop_entry, leaf);
			if (!leaf_reg || !bpf_reg_union_compatible(reg, leaf_reg))
				return false;
		} else {
			return false;
		}
	}
	return true;
}

struct bounds {
	struct cnum64 r64;
	struct cnum32 r32;
	u16 base;
	u16 step;
};

static void log_bounds(struct bpf_verifier_env *env, const struct bounds *bounds)
{
	struct bpf_verifier_log *log = &env->log;

	bpf_verbose_snum(env, cnum64_smin(bounds->r64));
	bpf_log(log, "..");
	bpf_verbose_snum(env, cnum64_smax(bounds->r64));
	if (bounds->r32.size < U32_MAX) {
		bpf_log(log, " (s32 ");
		bpf_verbose_snum(env, cnum32_smin(bounds->r32));
		bpf_log(log, "..");
		bpf_verbose_snum(env, cnum32_smax(bounds->r32));
		bpf_log(log, ")");
	}
	bpf_log(log, " step %u", bounds->step);
}

struct linear_coefs {
	u32 base_reg;
	s64 slope;
	bool zext32;
};

static bool is_simple_linear(struct scev *scev, u32 id, struct linear_coefs *out)
{
	u32 base, slope;

	return is_linear_or_linear32(scev, id, &base, &slope, &out->zext32) &&
	       is_reg(scev, base, &out->base_reg) &&
	       is_imm(scev, slope, &out->slope) &&
	       out->slope <= S16_MAX &&
	       out->slope >= S16_MIN &&
	       out->slope != 0;
}

static struct cnum64 cnum64_zext32(struct cnum32 r32)
{
	return cnum64_from_urange(cnum32_umin(r32), cnum32_umax(r32));
}

/* Compute the range an induction variable in `reg` spans over the loop. */
static void linear_bounds(struct bpf_reg_state *reg, u32 max_header_count,
			  struct linear_coefs *coefs, struct bounds *out)
{
	s64 slope = coefs->slope;
	s64 slope_abs = slope < 0 ? -slope : slope;
	s64 min_val = reg_smin(reg);
	s64 max_val = reg_smax(reg);
	struct cnum64 r64;
	struct cnum32 r32;
	s64 total_change;
	bool overflow;

	overflow = check_mul_overflow(slope, (s64)max_header_count - 1, &total_change) ||
		   (slope > 0 && check_add_overflow(max_val, total_change, &max_val)) ||
		   (slope < 0 && check_add_overflow(min_val, total_change, &min_val));
	r64 = overflow ? CNUM64_UNBOUNDED : cnum64_from_srange(min_val, max_val);
	r32 = CNUM32_UNBOUNDED;
	if (coefs->zext32) {
		r32 = cnum32_from_cnum64(r64);
		r64 = cnum64_zext32(r32);
		/*
		 * 32-bit iteration overflow is equivalent to subtracting N * 2**32:
		 * (base + slope * k) % slope ≠ (base + slope * k - N * 2**32) % slope.
		 */
		overflow |= cnum32_contains(r32, 0) && cnum32_contains(r32, U32_MAX);
		/*
		 * The entry and zero-extended values must agree on how they define `base`,
		 * otherwise `base + slope * k` would define different sets of points between
		 * the two.
		 */
		overflow |= imod(reg_smin(reg), slope_abs) !=
			    imod(cnum64_smin(r64), slope_abs);
		/*
		 * Full range is a union of a reg's value at loop entry,
		 * plus a range produced by 32-bit iteration.
		 */
		r64 = cnum64_union(reg->r64, r64);
		r32 = cnum32_union(reg->r32, r32);
	}
	/*
	 * If the entry value is a single point the value set is 'v + slope * k',
	 * so the step is |slope|. Otherwise, only the power-of-two alignment
	 * shared by the entry value and the slope.
	 */
	if (cnum64_is_const(reg->r64) && !overflow) {
		out->step = slope_abs;
		out->base = imod(reg_smin(reg), out->step);
	} else {
		out->step = 1u << min_t(u32, tnum_alignment(reg->var_off), __ffs(slope_abs));
		out->base = 0;
	}
	out->r64 = r64;
	out->r32 = r32;
}

static int mark_expr_precise(struct bpf_verifier_env *env, struct bpf_func_state *st, u32 id,
			     unsigned long *precise_regs)
{
	struct scev *scev = env->scev;
	struct bpf_reg_state *reg;
	u32 r, order;

	scev->stack_sz = 0;
	expr_stack_push(scev, id);
	while (expr_next(scev, &id, &order)) {
		if (order & DEPTH_LIMIT) {
			verifier_bug(env, "scev precision marking exceeds expression depth limit");
			return -EFAULT;
		}
		if (!(order & PRE) || !is_reg(scev, id, &r))
			continue;
		/* Synthetic values have no spilled register to mark precise. */
		reg = scev_reg_for_linear(st, r);
		if (reg && reg->type == SCALAR_VALUE)
			__set_bit(r, precise_regs);
	}
	return 0;
}

int bpf_compute_loop_iters(struct bpf_verifier_env *env, struct bpf_verifier_state *st,
			   struct bpf_loop_iters *iters)
{
	struct bpf_func_state *cur_func = st->frame[st->curframe];
	struct bpf_insn_aux_data *aux = env->insn_aux_data;
	struct bpf_verifier_log *log = &env->log;
	struct scev *scev = env->scev;
	struct env *header_env;
	struct linear_latch latch, best;
	struct linear_coefs coefs;
	struct bpf_backedge *backedge;
	struct bpf_loop_iters tmp;
	struct bpf_loop *loop;
	int insn_idx = st->insn_idx;
	bool found = false;
	int linear_latch;
	int latch_idx;
	int err, j;
	u32 r;

	if (!env->allow_uninit_stack)
		return 0;

	/*
	 * If insn_idx is a loop header for a reducible loop with a single backedge.
	 * loop is NULL for secondary entries to irreducible loops.
	 */
	loop = aux[insn_idx].loop;
	if (!loop || loop->irreducible || loop->backedges_cnt != 1 || loop->backedges_overflow ||
	    loop->exits_overflow) {
		if (log->level & BPF_LOG_LEVEL2)
			bpf_log(log, "loop header at %d, unsupported loop:%s%s%s\n", insn_idx,
				!loop || loop->irreducible ? " irreducible" : "",
				loop && loop->backedges_cnt > 1 ? " multiple backedges" : "",
				loop && loop->exits_overflow ? " too many exits" : "");
		return 0;
	}

	/* If this backedge has a latch */
	backedge = &loop->backedges[0];
	if (!backedge->latches_cnt) {
		if (log->level & BPF_LOG_LEVEL2)
			bpf_log(log, "loop header at %d, latch not identified\n", insn_idx);
		return 0;
	}

	/*
	 * Every condition dominating the backedge bounds the number of
	 * iterations from above, keep the smallest bound.
	 */
	for (j = 0; j < backedge->latches_cnt; j++) {
		latch_idx = backedge->latches[j];
		linear_latch = match_linear_latch(env, insn_idx, latch_idx, &latch);
		if (linear_latch < 0)
			return linear_latch;
		if (!linear_latch) {
			if (log->level & BPF_LOG_LEVEL2)
				bpf_log(log, "loop header at %d, non-linear %s at %d\n",
					insn_idx, j ? "exit" : "latch", latch_idx);
			continue;
		}
		if (!compute_max_iters(env, cur_func, &latch, &tmp)) {
			if (log->level & BPF_LOG_LEVEL2)
				bpf_log(log,
					"loop header at %d, can't compute iterations count at %d\n",
					insn_idx, latch_idx);
			continue;
		}
		if (!found || tmp.max_header_count < iters->max_header_count) {
			iters->max_header_count = tmp.max_header_count;
			best = latch;
		}
		iters->min_header_count = found ? min(iters->min_header_count, tmp.min_header_count)
						: tmp.min_header_count;
		found = true;
	}
	if (!found) {
		if (log->level & BPF_LOG_LEVEL2)
			bpf_log(log, "loop header at %d, can't compute iterations count\n", insn_idx);
		return 0;
	}
	latch = best;

	if (iters->max_header_count == 0) {
		if (log->level & BPF_LOG_LEVEL2)
			bpf_log(log, "loop header at %d, 0 iterations count\n", insn_idx);
		return 0;
	}

	if (iters->max_header_count == U32_MAX) {
		if (log->level & BPF_LOG_LEVEL2)
			bpf_log(log, "loop header at %d, inf iterations count\n", insn_idx);
		return 0;
	}

	if (log->level & BPF_LOG_LEVEL2) {
		bpf_log(log, "loop header at %d, header_count is ", insn_idx);
		if (iters->min_header_count == iters->max_header_count)
			bpf_log(log, "%u ", iters->max_header_count);
		else
			bpf_log(log, "[%u..%u] ", iters->min_header_count, iters->max_header_count);
		bpf_log(log, "\n");
	}

	err = bpf_live_stack_query_init(env, st);
	if (err)
		return err;

	header_env = find_header_env(scev, insn_idx);
	for (r = 0; r < REGS_NUM; r++) {
		struct bpf_reg_state *reg;
		u32 ra, r_scev, r_expr;
		bool spill_base = false;

		if (!scev_reg_alive(env, st, r))
			continue;
		/*
		 * A heuristic:
		 * Ignore SCEVs computed for unbound values and treat those as widened.
		 * This is poor man's modelling of e.g. bpf_probe_read_kernel() effect
		 * on stack, when called from a subprogram. Currently this would produce
		 * an unknown ('?') SCEV as a summary of a subprogram call effects.
		 * Ideally, this should be computed as an OPAQUE value.
		 * However, that would require tracking STACK_MISC writes in the
		 * liveness analysis. Not implemented yet.
		 */
		if (is_unbound_scev_reg(env, cur_func, r))
			continue;

		r_scev = header_env->reg2scev[r];
		reg = scev_reg_for_linear(cur_func, r);
		/* If SCEV for r is (linear <reg> <slope>) */
		if (reg && is_simple_linear(scev, r_scev, &coefs) && is_widenable_reg_type(reg)) {
			/* Spills at varying offsets lose precision */
			if (test_bit(r, loop->store_base_regs)) {
				spill_base = true;
				goto cant_widen;
			}
			continue;
		}

		/* rA = rA, loop does not change this reg */
		if (is_reg(scev, r_scev, &ra) && r == ra)
			continue;

		/* (any 1 (any 2 (any 3 4))) */
		if (is_widenable_any(env, cur_func, header_env, r, r_scev)) {
			if (test_bit(r, loop->store_base_regs)) {
				spill_base = true;
				goto cant_widen;
			}
			continue;
		}
cant_widen:
		if (log->level & BPF_LOG_LEVEL2) {
			r_expr = header_env->reg2expr[r];
			bpf_log(log, "loop header at %d, can't widen ", insn_idx);
			log_reg(env, r);
			bpf_log(log, ", expr is ");
			log_expr(env, r_expr);
			if (spill_base)
				bpf_log(log, ", requires exact stack-offset tracking");
			bpf_log(log, "\n");
		}
		return 0;
	}

	bitmap_zero(iters->precise_regs, REGS_NUM);
	err = mark_expr_precise(env, cur_func, latch.base_expr, iters->precise_regs);
	if (err)
		return err;
	err = mark_expr_precise(env, cur_func, latch.step_expr, iters->precise_regs);
	if (err)
		return err;
	err = mark_expr_precise(env, cur_func, latch.bound_expr, iters->precise_regs);
	if (err)
		return err;

	return 1;
}

/* bpf_compute_loop_iters() checked that every leaf can be unioned into acc. */
static int union_any_reg(struct bpf_verifier_env *env, struct bpf_func_state *loop_entry,
			 struct bpf_reg_state *acc, u32 id)
{
	struct bpf_reg_state *tmp = &env->fake_reg[0];
	const struct bpf_reg_state *leaf_reg;
	struct scev *scev = env->scev;
	u32 l, r, leaf, order;
	s64 imm;
	int err;

	scev->stack_sz = 0;
	expr_stack_push(scev, id);
	while (expr_next(scev, &id, &order)) {
		if (order & DEPTH_LIMIT) {
			verifier_bug(env, "scev ANY union exceeds expression depth limit");
			return -EFAULT;
		}
		if (!(order & PRE) || is_any(scev, id, &l, &r))
			continue;
		if (is_imm(scev, id, &imm)) {
			bpf_mark_reg_known_scalar(tmp, imm);
			leaf_reg = tmp;
		} else if (is_reg(scev, id, &leaf)) {
			leaf_reg = scev_reg_for_read(env, loop_entry, leaf);
			if (verifier_bug_if(!leaf_reg, env, "unreadable SCEV ANY leaf %u", leaf))
				return -EFAULT;
		} else {
			verifier_bug(env, "scev ANY union has an unsupported leaf");
			return -EFAULT;
		}
		err = bpf_reg_union(env, acc, leaf_reg);
		if (err)
			return err;
	}
	return 0;
}

int bpf_widen_scev_regs(struct bpf_verifier_env *env, struct bpf_verifier_state *st,
			struct bpf_verifier_state *loop_entry, struct bpf_loop_iters *iters)
{
	struct bpf_func_state *cur_func = st->frame[st->curframe];
	struct bpf_func_state *entry_func = loop_entry->frame[loop_entry->curframe];
	struct bpf_verifier_log *log = &env->log;
	struct scev *scev = env->scev;
	struct bpf_reg_state *reg;
	struct env *header_env;
	struct linear_coefs coefs;
	struct bounds bounds;
	u32 r, r_expr, a, b;
	int insn_idx = st->insn_idx;
	int err;

	for_each_set_bit(r, iters->precise_regs, REGS_NUM) {
		if (r < MAX_BPF_REG)
			bpf_bt_set_frame_reg(&env->bt, st->curframe, r);
		else
			bpf_bt_set_frame_slot(&env->bt, st->curframe, r - MAX_BPF_REG);
	}
	err = bpf_mark_chain_precision(env, st, -1, NULL);
	if (err)
		return err;

	header_env = find_header_env(scev, insn_idx);
	for (r = 0; r < REGS_NUM; r++) {
		if (!scev_reg_alive(env, st, r) || is_unbound_scev_reg(env, cur_func, r))
			continue;

		r_expr = header_env->reg2scev[r];
		/* If SCEV for r is (linear <reg> <slope>)*/
		if (is_simple_linear(scev, r_expr, &coefs) && coefs.base_reg == r) {
			err = scev_reg_for_write(env, cur_func, r, &reg);
			if (err)
				return err;
			linear_bounds(reg, iters->max_header_count, &coefs, &bounds);
			if (log->level & BPF_LOG_LEVEL2) {
				bpf_log(log, "loop header at %d, widening ", insn_idx);
				log_reg(env, r);
				bpf_log(log, " to ");
				log_bounds(env, &bounds);
				bpf_log(log, "\n");
			}
			scratch_widened_reg_id(env, reg);
			err = bpf_set_reg_range(env, reg, bounds.r64, bounds.r32,
						bounds.base, bounds.step);
			if (err)
				return err;
			mark_scev_reg_scratched(env, r);
		} else if (is_any(scev, r_expr, &a, &b)) {
			err = scev_reg_for_write(env, cur_func, r, &reg);
			if (err)
				return err;
			err = union_any_reg(env, entry_func, reg, r_expr);
			if (err)
				return err;
			if (log->level & BPF_LOG_LEVEL2) {
				bpf_log(log, "loop header at %d, widening ", insn_idx);
				log_reg(env, r);
				bpf_log(log, " to ");
				bpf_verbose_snum(env, reg_smin(reg));
				bpf_log(log, "..");
				bpf_verbose_snum(env, reg_smax(reg));
				bpf_log(log, " step %u\n", reg->step);
			}
			scratch_widened_reg_id(env, reg);
			mark_scev_reg_scratched(env, r);
		}
	}
	return 1;
}

int bpf_clamp_scev_regs(struct bpf_verifier_env *env, struct bpf_func_state *cur_func_state, u32 insn_idx,
			struct bpf_verifier_state *entry_state, u32 max_header_count)
{
	struct bpf_func_state *entry_st = entry_state->frame[entry_state->curframe];
	struct bpf_verifier_log *log = &env->log;
	struct bpf_reg_state *reg, *entry_reg;
	struct scev *scev = env->scev;
	struct linear_coefs coefs;
	struct env *header_env;
	struct bounds bounds;
	u32 r;
	int err;

	if (entry_state->curframe != cur_func_state->frameno) {
		verifier_bug(env, "clamping registers for a wrong frame: %d vs %d\n",
			     entry_state->curframe, cur_func_state->frameno);
		return -EFAULT;
	}

	header_env = find_header_env(scev, insn_idx);
	for (r = 0; r < REGS_NUM; r++) {
		/* If SCEV for r is (linear <reg> <slope>)*/
		if (!is_simple_linear(scev, header_env->reg2scev[r], &coefs) ||
		    coefs.base_reg != r)
			continue;

		entry_reg = scev_reg_for_linear(entry_st, r);
		reg = scev_reg_for_linear(cur_func_state, r);
		if (!entry_reg || !reg || entry_reg->type == NOT_INIT || reg->type == NOT_INIT ||
		    is_unbound_scev_reg(env, entry_st, r))
			continue;

		linear_bounds(entry_reg, max_header_count, &coefs, &bounds);
		bounds.r64 = cnum64_intersect(reg->r64, bounds.r64);
		bounds.r32 = cnum32_intersect(reg->r32, bounds.r32);
		if (cnum64_is_empty(bounds.r64)) {
			verifier_bug(env, "scev clamp produced empty range for r%d", r);
			return -EFAULT;
		}
		if (log->level & BPF_LOG_LEVEL2) {
			bpf_log(log, "loop header at %d, clamping ", insn_idx);
			log_reg(env, r);
			bpf_log(log, " to ");
			log_bounds(env, &bounds);
			bpf_log(log, "\n");
		}
		scratch_widened_reg_id(env, reg);
		err = bpf_set_reg_range(env, reg, bounds.r64, bounds.r32,
					bounds.base, bounds.step);
		if (err)
			return err;
		mark_scev_reg_scratched(env, r);
	}
	return 0;
}
