// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Isovalent */

#include <endian.h>
#include <stdint.h>
#include <test_progs.h>
#include <bpf/btf.h>
#include <linux/filter.h>

#include "sysctl_helpers.h"
#include "jit_insns_arena.skel.h"

#define NR_CASES	512
#define CTX_SPILL	-16	/* stack slot holding the ctx pointer */
#define MEM_SLOT	-8	/* stack slot for the atomic operations */

struct sweep_ctx {
	__u64 in[NR_CASES][2];
	__u64 out[NR_CASES][3];
};

#define IN_OFF(k, j)	(offsetof(struct sweep_ctx, in) + ((k) * 2 + (j)) * 8)
#define OUT_OFF(k, j)	(offsetof(struct sweep_ctx, out) + ((k) * 3 + (j)) * 8)

/* The cases of one program: operands in, results out, expected results */
struct batch {
	struct bpf_insn *insns;
	int cnt, cap;
	struct sweep_ctx ctx;
	__u64 exp[NR_CASES][3];
	__u8 nr_out[NR_CASES];
	int nr;
	char desc[80];
};

static char log[1 << 16];

#define EMIT(b, insn)	((b)->insns[(b)->cnt++] = (insn))

static void emit_ld_imm64(struct batch *b, int reg, __u64 imm)
{
	EMIT(b, BPF_RAW_INSN(BPF_LD | BPF_DW | BPF_IMM, reg, 0, 0, (__u32)imm));
	EMIT(b, BPF_RAW_INSN(0, 0, 0, 0, imm >> 32));
}

static void batch_reset(struct batch *b)
{
	b->cnt = 0;
	b->nr = 0;
	/* The ctx pointer lives on the stack, every register is an operand */
	EMIT(b, BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_1, CTX_SPILL));
}

static bool batch_init(struct batch *b)
{
	b->cap = NR_CASES * 24 + 8;
	b->insns = malloc(b->cap * sizeof(*b->insns));
	if (!ASSERT_OK_PTR(b->insns, "insns"))
		return false;
	batch_reset(b);
	return true;
}

static void batch_fini(struct batch *b)
{
	free(b->insns);
}

static int load_prog(struct bpf_insn *insns, int cnt, const char *desc)
{
	LIBBPF_OPTS(bpf_prog_load_opts, lopts,
		.prog_flags = BPF_F_SLEEPABLE,
		.log_buf = log,
		.log_size = sizeof(log),
	);
	int fd;

	fd = bpf_prog_load(BPF_PROG_TYPE_SYSCALL, NULL, "GPL", insns, cnt, &lopts);
	if (!ASSERT_GE(fd, 0, desc))
		fprintf(stderr, "%s", log);
	return fd;
}

/* Run the batch, check the results, start a new one */
static void batch_flush(struct batch *b)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts,
		.ctx_in = &b->ctx,
		.ctx_size_in = sizeof(b->ctx),
	);
	char buf[160];
	int fd, k, j;

	if (!b->nr)
		return;
	EMIT(b, BPF_MOV64_IMM(BPF_REG_0, 0));
	EMIT(b, BPF_EXIT_INSN());
	snprintf(buf, sizeof(buf), "%s: load %d cases", b->desc, b->nr);
	fd = load_prog(b->insns, b->cnt, buf);
	if (fd >= 0 && ASSERT_OK(bpf_prog_test_run_opts(fd, &opts), b->desc)) {
		for (k = 0; k < b->nr; k++) {
			for (j = 0; j < b->nr_out[k]; j++) {
				if (b->ctx.out[k][j] == b->exp[k][j])
					continue;
				snprintf(buf, sizeof(buf), "%s: in0=0x%llx in1=0x%llx out%d",
					 b->desc, b->ctx.in[k][0], b->ctx.in[k][1], j);
				ASSERT_EQ(b->ctx.out[k][j], b->exp[k][j], buf);
			}
		}
	}
	if (fd >= 0)
		close(fd);
	batch_reset(b);
}

/* Add a case: record the operands and load the ctx pointer into rc */
static int batch_case(struct batch *b, int rc, __u64 in0, __u64 in1)
{
	int k;

	if (b->nr == NR_CASES)
		batch_flush(b);
	k = b->nr++;
	b->ctx.in[k][0] = in0;
	b->ctx.in[k][1] = in1;
	b->nr_out[k] = 0;
	EMIT(b, BPF_LDX_MEM(BPF_DW, rc, BPF_REG_10, CTX_SPILL));
	return k;
}

static void batch_expect(struct batch *b, int k, int j, __u64 val)
{
	b->exp[k][j] = val;
	if (b->nr_out[k] <= j)
		b->nr_out[k] = j + 1;
}

#define LD_IN(b, k, j, rc, reg)		EMIT(b, BPF_LDX_MEM(BPF_DW, reg, rc, IN_OFF(k, j)))
#define ST_OUT(b, k, j, rc, reg)	EMIT(b, BPF_STX_MEM(BPF_DW, rc, reg, OUT_OFF(k, j)))

/* A register that is neither of the two operand registers nor r0 */
static int scratch_reg(int r1, int r2)
{
	int r;

	for (r = BPF_REG_1; r <= BPF_REG_9; r++)
		if (r != r1 && r != r2)
			return r;
	return BPF_REG_9;
}

static const char *alu_name(int op)
{
	switch (BPF_OP(op)) {
	case BPF_MOV: return "mov";
	case BPF_NEG: return "neg";
	case BPF_AND: return "and";
	case BPF_OR: return "or";
	case BPF_XOR: return "xor";
	case BPF_LSH: return "lsh";
	case BPF_RSH: return "rsh";
	case BPF_ARSH: return "arsh";
	case BPF_ADD: return "add";
	case BPF_SUB: return "sub";
	case BPF_MUL: return "mul";
	case BPF_DIV: return "div";
	case BPF_MOD: return "mod";
	case BPF_OP(BPF_XCHG): return "xchg";
	case BPF_OP(BPF_CMPXCHG): return "cmpxchg";
	default: return "?";
	}
}

static const char *jmp_name(int op)
{
	switch (op) {
	case BPF_JSET: return "jset";
	case BPF_JEQ: return "jeq";
	case BPF_JNE: return "jne";
	case BPF_JGT: return "jgt";
	case BPF_JGE: return "jge";
	case BPF_JLT: return "jlt";
	case BPF_JLE: return "jle";
	case BPF_JSGT: return "jsgt";
	case BPF_JSGE: return "jsge";
	case BPF_JSLT: return "jslt";
	case BPF_JSLE: return "jsle";
	default: return "?";
	}
}

/* ALU result computation, false when the operands are not valid */
enum { F_ALU32 = 1, F_SIGNED = 2 };

static bool alu_result(__u64 *res, __u64 v1, __u64 v2, __u8 op, __u32 flags)
{
	bool is_signed = flags & F_SIGNED;

	/* Narrow operands for ALU32 */
	if (flags & F_ALU32) {
		if (is_signed) {
			v1 = (__u64)(__s32)v1;
			v2 = (__u64)(__s32)v2;
		} else {
			v1 = (__u32)v1;
			v2 = (__u32)v2;
		}
	}

	*res = 0;
	switch (op) {
	case BPF_MOV:
		*res = v2;
		break;
	case BPF_NEG:
		*res = -v1;
		break;
	case BPF_AND:
		*res = v1 & v2;
		break;
	case BPF_OR:
		*res = v1 | v2;
		break;
	case BPF_XOR:
		*res = v1 ^ v2;
		break;
	case BPF_LSH:
		*res = v1 << v2;
		break;
	case BPF_RSH:
		*res = v1 >> v2;
		break;
	case BPF_ARSH:
		*res = v1 >> v2;
		if (v2 > 0 && v1 > INT64_MAX)
			*res |= ~0ULL << (64 - v2);
		break;
	case BPF_ADD:
		*res = v1 + v2;
		break;
	case BPF_SUB:
		*res = v1 - v2;
		break;
	case BPF_MUL:
		*res = v1 * v2;
		break;
	case BPF_DIV:
		if (v2 == 0)
			return false;
		if (!is_signed) {
			*res = v1 / v2;
		} else {
			if ((__s64)v2 == -1) /* Handled by the verifier */
				return false;
			*res = (__u64)((__s64)v1 / (__s64)v2);
		}
		break;
	case BPF_MOD:
		if (v2 == 0)
			return false;
		if (!is_signed) {
			*res = v1 % v2;
		} else {
			if ((__s64)v2 == -1)
				return false;
			*res = (__u64)((__s64)v1 % (__s64)v2);
		}
		break;
	}
	return true;
}

/*
 * Operand pattern generator for the power-of-two magnitude tests: all
 * combinations of magnitudes and signs with a block of contiguous values
 * around each magnitude, with every dstep'th magnitude of the destination,
 * and a larger block for the same magnitude of both operands. The first
 * pattern is where the immediates take every encoding a JIT has, the
 * second where the sign and overflow boundaries of both operands meet.
 */
static __s64 value(int msb, int delta, int sign)
{
	return sign * (1LL << msb) + delta;
}

#define PATTERN_BLOCK1 1
#define PATTERN_BLOCK2 3

typedef void (*emit_fn)(struct batch *b, void *arg, __s64 dst, __s64 src);

static void fill_pattern(struct batch *b, void *arg, int dbits, int sbits,
			 int block1, int dstep, int block2, emit_fn emit)
{
	static const int sgn[][2] = { { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } };
	int di, si, bt, db, sb, k;

	for (di = 0; di < dbits - 1; di += dstep)
		for (si = 0; si < sbits - 1; si++)
			for (k = 0; k < ARRAY_SIZE(sgn); k++)
				for (db = -(block1 / 2); db < (block1 + 1) / 2; db++)
					for (sb = -(block1 / 2); sb < (block1 + 1) / 2; sb++)
						emit(b, arg, value(di, db, sgn[k][0]),
						     value(si, sb, sgn[k][1]));

	for (bt = 0; bt < (dbits > sbits ? dbits : sbits) - 1; bt++)
		for (k = 0; k < ARRAY_SIZE(sgn); k++)
			for (db = -(block2 / 2); db < (block2 + 1) / 2; db++)
				for (sb = -(block2 / 2); sb < (block2 + 1) / 2; sb++)
					emit(b, arg, value(bt % dbits, db, sgn[k][0]),
					     value(bt % sbits, sb, sgn[k][1]));
	batch_flush(b);
}

/* ALU operations with an immediate and with a register for all magnitudes */
static const int alu_ops[] = {
	BPF_MOV, BPF_AND, BPF_OR, BPF_XOR, BPF_ADD, BPF_SUB, BPF_MUL, BPF_DIV, BPF_MOD,
};

struct alu_arg {
	int op;
	__u32 flags;
};

static void emit_alu_imm(struct batch *b, void *arg, __s64 dst, __s64 imm)
{
	struct alu_arg *a = arg;
	bool alu32 = a->flags & F_ALU32;
	__u64 res;
	int k;

	if (!alu_result(&res, dst, (__s32)imm, a->op, a->flags))
		return;
	k = batch_case(b, BPF_REG_3, dst, 0);
	LD_IN(b, k, 0, BPF_REG_3, BPF_REG_1);
	EMIT(b, BPF_RAW_INSN((alu32 ? BPF_ALU : BPF_ALU64) | BPF_OP(a->op) | BPF_K,
			     BPF_REG_1, 0, a->flags & F_SIGNED ? 1 : 0, imm));
	ST_OUT(b, k, 0, BPF_REG_3, BPF_REG_1);
	batch_expect(b, k, 0, alu32 ? (__u32)res : res);
}

static void emit_alu_reg(struct batch *b, void *arg, __s64 dst, __s64 src)
{
	struct alu_arg *a = arg;
	bool alu32 = a->flags & F_ALU32;
	__u64 res;
	int k;

	if (!alu_result(&res, dst, src, a->op, a->flags))
		return;
	k = batch_case(b, BPF_REG_3, dst, src);
	LD_IN(b, k, 0, BPF_REG_3, BPF_REG_1);
	LD_IN(b, k, 1, BPF_REG_3, BPF_REG_2);
	EMIT(b, BPF_RAW_INSN((alu32 ? BPF_ALU : BPF_ALU64) | BPF_OP(a->op) | BPF_X,
			     BPF_REG_1, BPF_REG_2, a->flags & F_SIGNED ? 1 : 0, 0));
	ST_OUT(b, k, 0, BPF_REG_3, BPF_REG_1);
	batch_expect(b, k, 0, alu32 ? (__u32)res : res);
}

static void test_alu_magnitudes(bool reg)
{
	struct batch b;
	int o, f;

	if (!batch_init(&b))
		return;
	for (f = 0; f < 2; f++) {
		for (o = 0; o < ARRAY_SIZE(alu_ops); o++) {
			struct alu_arg a = { alu_ops[o], f ? F_ALU32 : 0 };

			snprintf(b.desc, sizeof(b.desc), "alu%d_%s_%c magnitudes",
				 f ? 32 : 64, alu_name(a.op), reg ? 'x' : 'k');
			if (reg)
				fill_pattern(&b, &a, 64, 64, 0, 1, PATTERN_BLOCK2, emit_alu_reg);
			else
				fill_pattern(&b, &a, 64, 32, PATTERN_BLOCK1, 1, 5, emit_alu_imm);
		}
		for (o = 0; o < 2; o++) {
			struct alu_arg a = { o ? BPF_MOD : BPF_DIV, F_SIGNED | (f ? F_ALU32 : 0) };

			snprintf(b.desc, sizeof(b.desc), "alu%d_s%s_%c magnitudes",
				 f ? 32 : 64, alu_name(a.op), reg ? 'x' : 'k');
			if (reg)
				fill_pattern(&b, &a, 64, 64, 0, 1, PATTERN_BLOCK2, emit_alu_reg);
			else
				fill_pattern(&b, &a, 64, 32, PATTERN_BLOCK1, 1, 5, emit_alu_imm);
		}
	}
	batch_fini(&b);
}

static void test_alu_imm(void)
{
	test_alu_magnitudes(false);
}

static void test_alu_reg(void)
{
	test_alu_magnitudes(true);
}

/* ALU operations with an immediate for every destination register */
static const int alu_reg_ops[] = {
	BPF_MOV, BPF_AND, BPF_OR, BPF_XOR, BPF_LSH, BPF_RSH, BPF_ARSH,
	BPF_ADD, BPF_SUB, BPF_MUL, BPF_DIV, BPF_MOD,
};

static void alu_imm_regs(struct batch *b, int op, __u32 flags)
{
	bool alu32 = flags & F_ALU32;
	__u32 imm = 0x01234567U;
	__u64 dst, res;
	int rd, rc, k;

	if (flags & F_SIGNED)
		dst = alu32 ? (__u64)-76543210LL : (__u64)-7654321076543210LL;
	else
		dst = alu32 ? 0x76543210ULL : 0x7edcba9876543210ULL;
	if (op == BPF_LSH || op == BPF_RSH || op == BPF_ARSH)
		imm &= 31;
	if (op == BPF_NEG)
		imm = 0;
	alu_result(&res, dst, imm, op, flags);
	if (alu32)
		res = (__u32)res;

	snprintf(b->desc, sizeof(b->desc), "alu%d_%s%s_k registers", alu32 ? 32 : 64,
		 flags & F_SIGNED ? "s" : "", alu_name(op));
	for (rd = BPF_REG_0; rd <= BPF_REG_9; rd++) {
		rc = scratch_reg(rd, rd);
		k = batch_case(b, rc, dst, 0);
		LD_IN(b, k, 0, rc, rd);
		EMIT(b, BPF_RAW_INSN((alu32 ? BPF_ALU : BPF_ALU64) | BPF_OP(op) | BPF_K,
				     rd, 0, flags & F_SIGNED ? 1 : 0, imm));
		ST_OUT(b, k, 0, rc, rd);
		batch_expect(b, k, 0, res);
	}
	batch_flush(b);
}

static void test_alu_imm_regs(void)
{
	struct batch b;
	int o, f;

	if (!batch_init(&b))
		return;
	for (f = 0; f < 2; f++) {
		for (o = 0; o < ARRAY_SIZE(alu_reg_ops); o++)
			alu_imm_regs(&b, alu_reg_ops[o], f ? F_ALU32 : 0);
		alu_imm_regs(&b, BPF_NEG, f ? F_ALU32 : 0);
		alu_imm_regs(&b, BPF_DIV, F_SIGNED | (f ? F_ALU32 : 0));
		alu_imm_regs(&b, BPF_MOD, F_SIGNED | (f ? F_ALU32 : 0));
	}
	batch_fini(&b);
}

/*
 * Division and modulo by constants for every width, signedness and
 * destination register with a set of divisors and dividends around the
 * multiple boundaries and the extremes of each range. This covers a JIT
 * replacing the division with a multiplication by a magic number in all
 * of its forms: magic numbers needing the extra add step, zero shifts,
 * both signed corrections, and the divisors like 1, -1 and the ones
 * above 2^63 that are still emitted as a real division. The dividends
 * of the 32-bit operations carry garbage in the upper 32 bits.
 */
static const __s32 unsigned_divisors[] = {
	2, 3, 7, 10, 25, 641, 1000, 4096, 65536, 1000000007, 0x7fffffff,
	-2, -10, -0x7fffffff - 1, -1, 1,
};

static const __s32 signed_divisors[] = {
	2, 3, 7, 25, 1000, 0x40000000, 0x7fffffff,
	-2, -3, -5, -7, -1000, -0x7fffffff, -0x7fffffff - 1, 1, -1,
};

static int dividends(__u32 flags, __s32 imm, __u64 *n)
{
	bool alu32 = flags & F_ALU32, is_signed = flags & F_SIGNED;
	__u64 d = is_signed ? (imm < 0 ? -(__s64)imm : imm) :
		  alu32 ? (__u32)imm : (__u64)(__s64)imm;
	int cnt = 0, i;

	n[cnt++] = 0;
	n[cnt++] = d - 1;
	n[cnt++] = d + 1;
	n[cnt++] = 3 * d + 2;
	if (!alu32) {
		n[cnt++] = (d << 40) - 1;
		n[cnt++] = (d << 40) + 1;
		n[cnt++] = (1ULL << 63) - 1;
		n[cnt++] = 1ULL << 63;
		n[cnt++] = ~0ULL;
		n[cnt++] = 0xdaeb8ebd244a330cULL;
	} else {
		n[cnt++] = (d << 16) - 1;
		n[cnt++] = (d << 16) + 1;
		n[cnt++] = (1ULL << 31) - 1;
		n[cnt++] = 1ULL << 31;
		n[cnt++] = ~0U;
		n[cnt++] = 0x8c1e86f1;
	}
	if (is_signed) {
		n[cnt++] = -1;
		n[cnt++] = -d - 1;
		n[cnt++] = -3 * d - 2;
		n[cnt++] = alu32 ? -(1ULL << 31) + 1 : -(1ULL << 63) + 1;
	}
	if (alu32)
		for (i = 0; i < cnt; i++)
			n[i] = (__u32)n[i] | ((0xa5a50000ULL + i) << 32);
	return cnt;
}

static __u64 div_result(int op, __u32 flags, __s32 imm, __u64 n)
{
	__u64 res;

	/* LLONG_MIN / -1 == LLONG_MIN, LLONG_MIN % -1 == 0, same for INT_MIN */
	if ((flags & F_SIGNED) && imm == -1)
		res = op == BPF_MOD ? 0 : -n;
	else
		alu_result(&res, n, (__s32)imm, op, flags);
	return flags & F_ALU32 ? (__u32)res : res;
}

static void div_const(struct batch *b, int op, __u32 flags)
{
	bool alu32 = flags & F_ALU32, is_signed = flags & F_SIGNED;
	const __s32 *divs = is_signed ? signed_divisors : unsigned_divisors;
	int cnt = is_signed ? ARRAY_SIZE(signed_divisors) : ARRAY_SIZE(unsigned_divisors);
	int code = (alu32 ? BPF_ALU : BPF_ALU64) | BPF_OP(op) | BPF_K;
	int i, j, nr, rd, rc, k;
	__u64 n[16];

	snprintf(b->desc, sizeof(b->desc), "alu%d_%s%s_k constants", alu32 ? 32 : 64,
		 is_signed ? "s" : "", alu_name(op));
	for (i = 0; i < cnt; i++) {
		nr = dividends(flags, divs[i], n);
		for (rd = BPF_REG_0; rd <= BPF_REG_9; rd++) {
			rc = scratch_reg(rd, rd);
			for (j = 0; j < nr; j++) {
				k = batch_case(b, rc, n[j], divs[i]);
				LD_IN(b, k, 0, rc, rd);
				EMIT(b, BPF_RAW_INSN(code, rd, 0, is_signed, divs[i]));
				ST_OUT(b, k, 0, rc, rd);
				batch_expect(b, k, 0, div_result(op, flags, divs[i], n[j]));
			}
		}
	}
	batch_flush(b);
}

static void test_div_const(void)
{
	struct batch b;
	int f;

	if (!batch_init(&b))
		return;
	for (f = 0; f < 2; f++) {
		div_const(&b, BPF_DIV, f ? F_ALU32 : 0);
		div_const(&b, BPF_MOD, f ? F_ALU32 : 0);
		div_const(&b, BPF_DIV, F_SIGNED | (f ? F_ALU32 : 0));
		div_const(&b, BPF_MOD, F_SIGNED | (f ? F_ALU32 : 0));
	}
	batch_fini(&b);
}

/* ALU operations for all combinations of operand registers */
static void alu_reg_pairs(struct batch *b, int op, __u32 flags)
{
	bool alu32 = flags & F_ALU32;
	__u64 dst, src, res, same;
	int rd, rs, rc, k;

	if (flags & F_SIGNED)
		dst = alu32 ? (__u64)-76543210LL : (__u64)-7654321076543210LL;
	else
		dst = alu32 ? 0x76543210ULL : 0x7edcba9876543210ULL;
	src = alu32 ? 0x01234567ULL : 0x0123456789abcdefULL;
	if (op == BPF_LSH || op == BPF_RSH || op == BPF_ARSH)
		src &= 31;
	alu_result(&res, dst, src, op, flags);
	alu_result(&same, src, src, op, flags);
	if (alu32) {
		res = (__u32)res;
		same = (__u32)same;
	}

	snprintf(b->desc, sizeof(b->desc), "alu%d_%s%s_x register pairs", alu32 ? 32 : 64,
		 flags & F_SIGNED ? "s" : "", alu_name(op));
	for (rd = BPF_REG_0; rd <= BPF_REG_9; rd++) {
		for (rs = BPF_REG_0; rs <= BPF_REG_9; rs++) {
			rc = scratch_reg(rd, rs);
			k = batch_case(b, rc, rd == rs ? src : dst, src);
			LD_IN(b, k, 0, rc, rd);
			if (rs != rd)
				LD_IN(b, k, 1, rc, rs);
			EMIT(b, BPF_RAW_INSN((alu32 ? BPF_ALU : BPF_ALU64) | BPF_OP(op) | BPF_X,
					     rd, rs, flags & F_SIGNED ? 1 : 0, 0));
			ST_OUT(b, k, 0, rc, rd);
			batch_expect(b, k, 0, rd == rs ? same : res);
		}
	}
	batch_flush(b);
}

static void test_alu_reg_pairs(void)
{
	struct batch b;
	int o, f;

	if (!batch_init(&b))
		return;
	for (f = 0; f < 2; f++) {
		for (o = 0; o < ARRAY_SIZE(alu_reg_ops); o++)
			alu_reg_pairs(&b, alu_reg_ops[o], f ? F_ALU32 : 0);
		alu_reg_pairs(&b, BPF_DIV, F_SIGNED | (f ? F_ALU32 : 0));
		alu_reg_pairs(&b, BPF_MOD, F_SIGNED | (f ? F_ALU32 : 0));
	}
	batch_fini(&b);
}

/* Shifts for all shift counts, by immediate, by register and by the shifted register */
static void test_shifts(void)
{
	static const __s64 vals[] = {
		0x0123456789abcdefLL, /* dword > 0, word < 0 */
		0xfedcba9876543210LL, /* dword < 0, word > 0 */
		0xfedcba0198765432LL, /* dword < 0, word < 0 */
		0x0123458967abcdefLL, /* dword > 0, word > 0 */
	};
	static const int ops[] = { BPF_LSH, BPF_RSH, BPF_ARSH };
	struct batch b;
	int o, f, v, imm, k;

	if (!batch_init(&b))
		return;
	for (f = 0; f < 2; f++) {
		int bits = f ? 32 : 64;

		for (o = 0; o < ARRAY_SIZE(ops); o++) {
			int op = ops[o];

			snprintf(b.desc, sizeof(b.desc), "alu%d_%s all shift counts", bits,
				 alu_name(op));
			for (v = 0; v < ARRAY_SIZE(vals); v++) {
				for (imm = 0; imm < bits; imm++) {
					__u64 reg = vals[v], res;

					if (f)
						reg = op == BPF_ARSH ? (__u64)(__s32)reg :
						      (__u32)reg;
					alu_result(&res, reg, imm, op, 0);
					if (f)
						res = (__u32)res;

					k = batch_case(&b, BPF_REG_3, vals[v], imm);
					LD_IN(&b, k, 0, BPF_REG_3, BPF_REG_1);
					if (f)
						EMIT(&b, BPF_ALU32_IMM(op, BPF_REG_1, imm));
					else
						EMIT(&b, BPF_ALU64_IMM(op, BPF_REG_1, imm));
					ST_OUT(&b, k, 0, BPF_REG_3, BPF_REG_1);
					batch_expect(&b, k, 0, res);

					k = batch_case(&b, BPF_REG_3, vals[v], imm);
					LD_IN(&b, k, 0, BPF_REG_3, BPF_REG_1);
					LD_IN(&b, k, 1, BPF_REG_3, BPF_REG_2);
					if (f)
						EMIT(&b, BPF_ALU32_REG(op, BPF_REG_1, BPF_REG_2));
					else
						EMIT(&b, BPF_ALU64_REG(op, BPF_REG_1, BPF_REG_2));
					ST_OUT(&b, k, 0, BPF_REG_3, BPF_REG_1);
					batch_expect(&b, k, 0, res);
				}
			}
			/* The count in the shifted register */
			for (imm = 0; imm < bits; imm++) {
				__u64 res;

				alu_result(&res, imm, imm, op, 0);
				k = batch_case(&b, BPF_REG_3, imm, 0);
				LD_IN(&b, k, 0, BPF_REG_3, BPF_REG_1);
				if (f)
					EMIT(&b, BPF_ALU32_REG(op, BPF_REG_1, BPF_REG_1));
				else
					EMIT(&b, BPF_ALU64_REG(op, BPF_REG_1, BPF_REG_1));
				ST_OUT(&b, k, 0, BPF_REG_3, BPF_REG_1);
				batch_expect(&b, k, 0, f ? (__u32)res : res);
			}
			batch_flush(&b);
		}
	}
	batch_fini(&b);
}

/* Atomic operations on the stack for all magnitudes of memory and source operand */
static const int atomic_ops[] = {
	BPF_ADD, BPF_AND, BPF_OR, BPF_XOR,
	BPF_ADD | BPF_FETCH, BPF_AND | BPF_FETCH, BPF_OR | BPF_FETCH, BPF_XOR | BPF_FETCH,
	BPF_XCHG,
};

struct atomic_arg {
	int op;
	int size;
};

static void emit_atomic(struct batch *b, void *arg, __s64 dst, __s64 src)
{
	struct atomic_arg *a = arg;
	bool w = a->size == BPF_W;
	__u64 keep = 0x0123456789abcdefULL, res, fetch;
	int k;

	if (a->op == BPF_XCHG)
		res = src;
	else
		alu_result(&res, w ? (__u32)dst : dst, w ? (__u32)src : src, BPF_OP(a->op), 0);
	/* The source register gets the old value with fetch, keeps its own without */
	fetch = a->op & BPF_FETCH ? (w ? (__u32)dst : dst) : src;

	k = batch_case(b, BPF_REG_3, dst, src);
	LD_IN(b, k, 0, BPF_REG_3, BPF_REG_1);
	EMIT(b, BPF_STX_MEM(a->size, BPF_REG_10, BPF_REG_1, MEM_SLOT));
	LD_IN(b, k, 1, BPF_REG_3, BPF_REG_2);
	emit_ld_imm64(b, BPF_REG_0, keep);
	EMIT(b, BPF_ATOMIC_OP(a->size, a->op, BPF_REG_10, BPF_REG_2, MEM_SLOT));
	EMIT(b, BPF_LDX_MEM(a->size, BPF_REG_1, BPF_REG_10, MEM_SLOT));
	ST_OUT(b, k, 0, BPF_REG_3, BPF_REG_1);
	ST_OUT(b, k, 1, BPF_REG_3, BPF_REG_2);
	ST_OUT(b, k, 2, BPF_REG_3, BPF_REG_0);
	batch_expect(b, k, 0, w ? (__u32)res : res);
	batch_expect(b, k, 1, fetch);
	batch_expect(b, k, 2, keep);
}

static void emit_cmpxchg(struct batch *b, void *arg, __s64 dst, __s64 src)
{
	int size = *(int *)arg;
	bool w = size == BPF_W;
	__u64 old = w ? (__u32)dst : dst;
	int k;

	k = batch_case(b, BPF_REG_3, dst, src);
	LD_IN(b, k, 0, BPF_REG_3, BPF_REG_1);
	EMIT(b, BPF_STX_MEM(size, BPF_REG_10, BPF_REG_1, MEM_SLOT));
	LD_IN(b, k, 1, BPF_REG_3, BPF_REG_2);
	/* Mismatch: the memory keeps its value, r0 gets it */
	EMIT(b, BPF_MOV64_REG(BPF_REG_0, BPF_REG_1));
	EMIT(b, BPF_ALU64_IMM(BPF_XOR, BPF_REG_0, -1));
	EMIT(b, BPF_ATOMIC_OP(size, BPF_CMPXCHG, BPF_REG_10, BPF_REG_2, MEM_SLOT));
	ST_OUT(b, k, 0, BPF_REG_3, BPF_REG_0);
	/* Match: the memory gets the source, r0 the old value */
	EMIT(b, BPF_ATOMIC_OP(size, BPF_CMPXCHG, BPF_REG_10, BPF_REG_2, MEM_SLOT));
	EMIT(b, BPF_LDX_MEM(size, BPF_REG_1, BPF_REG_10, MEM_SLOT));
	ST_OUT(b, k, 1, BPF_REG_3, BPF_REG_1);
	ST_OUT(b, k, 2, BPF_REG_3, BPF_REG_0);
	batch_expect(b, k, 0, old);
	batch_expect(b, k, 1, w ? (__u32)src : src);
	batch_expect(b, k, 2, old);
}

static void test_atomics(void)
{
	static const int sizes[] = { BPF_DW, BPF_W };
	struct batch b;
	int o, s;

	if (!batch_init(&b))
		return;
	for (s = 0; s < ARRAY_SIZE(sizes); s++) {
		int size = sizes[s];

		for (o = 0; o < ARRAY_SIZE(atomic_ops); o++) {
			struct atomic_arg a = { atomic_ops[o], size };

			snprintf(b.desc, sizeof(b.desc), "atomic%d_%s%s magnitudes",
				 size == BPF_DW ? 64 : 32, alu_name(a.op),
				 a.op == BPF_XCHG ? "" : a.op & BPF_FETCH ? "_fetch" : "");
			fill_pattern(&b, &a, 64, 64, 0, 1, PATTERN_BLOCK2, emit_atomic);
		}
		snprintf(b.desc, sizeof(b.desc), "atomic%d_cmpxchg magnitudes",
			 size == BPF_DW ? 64 : 32);
		fill_pattern(&b, &size, 64, 64, 0, 1, PATTERN_BLOCK2, emit_cmpxchg);
	}
	batch_fini(&b);
}

/* Atomic operations for all combinations of pointer and source register */
static void atomic_reg_pairs(struct batch *b, int size, int op)
{
	bool w = size == BPF_W;
	__u64 mem, upd;
	int rd, rs, rc, k, n;

	if (w) {
		mem = 0x01234567U;
		upd = 0x76543210U;
	} else {
		mem = 0x0123456789abcdefULL;
		upd = 0xfedcba9876543210ULL;
	}

	snprintf(b->desc, sizeof(b->desc), "atomic%d_%s%s register pairs", w ? 32 : 64,
		 alu_name(op), op == BPF_XCHG || op == BPF_CMPXCHG ? "" :
		 op & BPF_FETCH ? "_fetch" : "");
	for (rd = BPF_REG_0; rd <= BPF_REG_9; rd++) {
		for (rs = BPF_REG_0; rs <= BPF_REG_9; rs++) {
			__u64 regs[10], old = mem, new, src, cmp;

			/* The source would be the pointer, r0 the compare value */
			if (rs == rd || (op == BPF_CMPXCHG && rd == BPF_REG_0))
				continue;
			rc = scratch_reg(rd, rs);
			k = batch_case(b, rc, mem, upd);
			/* Memory from in0, r0 inverted, source from in1, pointer */
			LD_IN(b, k, 0, rc, rs);
			EMIT(b, BPF_STX_MEM(size, BPF_REG_10, rs, MEM_SLOT));
			LD_IN(b, k, 0, rc, BPF_REG_0);
			EMIT(b, BPF_ALU64_IMM(BPF_XOR, BPF_REG_0, -1));
			LD_IN(b, k, 1, rc, rs);
			EMIT(b, BPF_MOV64_REG(rd, BPF_REG_10));
			EMIT(b, BPF_ATOMIC_OP(size, op, rd, rs, MEM_SLOT));
			/* r0 holds the pointer when it is the destination */
			n = 0;
			if (rd != BPF_REG_0)
				ST_OUT(b, k, n++, rc, BPF_REG_0);
			ST_OUT(b, k, n++, rc, rs);
			EMIT(b, BPF_LDX_MEM(size, rs, BPF_REG_10, MEM_SLOT));
			ST_OUT(b, k, n, rc, rs);

			/* Model with the register aliasing of r0 */
			regs[BPF_REG_0] = ~mem;
			regs[rs] = upd;
			src = w ? (__u32)regs[rs] : regs[rs];
			if (op == BPF_XCHG) {
				new = src;
				regs[rs] = old;
			} else if (op == BPF_CMPXCHG) {
				cmp = w ? (__u32)regs[BPF_REG_0] : regs[BPF_REG_0];
				new = cmp == old ? src : old;
				regs[BPF_REG_0] = old;
			} else {
				alu_result(&new, old, src, BPF_OP(op), 0);
				if (w)
					new = (__u32)new;
				if (op & BPF_FETCH)
					regs[rs] = old;
			}
			n = 0;
			if (rd != BPF_REG_0)
				batch_expect(b, k, n++, regs[BPF_REG_0]);
			batch_expect(b, k, n++, regs[rs]);
			batch_expect(b, k, n, new);
		}
	}
	batch_flush(b);
}

static void test_atomic_reg_pairs(void)
{
	static const int sizes[] = { BPF_DW, BPF_W };
	struct batch b;
	int o, s;

	if (!batch_init(&b))
		return;
	for (s = 0; s < ARRAY_SIZE(sizes); s++) {
		for (o = 0; o < ARRAY_SIZE(atomic_ops); o++)
			atomic_reg_pairs(&b, sizes[s], atomic_ops[o]);
		atomic_reg_pairs(&b, sizes[s], BPF_CMPXCHG);
	}
	batch_fini(&b);
}

/* A maximal chain of atomic adds on one stack slot */
static void test_xadd_chain(void)
{
	static const int sizes[] = { BPF_DW, BPF_W };
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	struct bpf_insn *insns;
	int len = BPF_MAXINSNS, s, i, fd;

	insns = malloc(len * sizeof(*insns));
	if (!ASSERT_OK_PTR(insns, "insns"))
		return;
	for (s = 0; s < ARRAY_SIZE(sizes); s++) {
		insns[0] = BPF_MOV32_IMM(BPF_REG_0, 1);
		insns[1] = BPF_ST_MEM(sizes[s], BPF_REG_10, -40, 42);
		for (i = 2; i < len - 2; i++)
			insns[i] = BPF_STX_XADD(sizes[s], BPF_REG_10, BPF_REG_0, -40);
		insns[len - 2] = BPF_LDX_MEM(sizes[s], BPF_REG_0, BPF_REG_10, -40);
		insns[len - 1] = BPF_EXIT_INSN();

		fd = load_prog(insns, len, "xadd chain");
		if (fd < 0)
			break;
		if (ASSERT_OK(bpf_prog_test_run_opts(fd, &opts), "xadd chain run"))
			ASSERT_EQ(opts.retval, 42 + len - 4, "xadd chain result");
		close(fd);
	}
	free(insns);
}

/* 64-bit immediate loads for all magnitudes and byte patterns */
static void ld_imm64_case(struct batch *b, __u64 imm)
{
	int k = batch_case(b, BPF_REG_3, imm, 0);

	emit_ld_imm64(b, BPF_REG_1, imm);
	ST_OUT(b, k, 0, BPF_REG_3, BPF_REG_1);
	batch_expect(b, k, 0, imm);
}

static void test_ld_imm64(void)
{
	static const struct {
		__u8 base1, mask1, base2, mask2;
	} bytes[] = {
		{ 0, 0xff, 0xff, 0xff },	/* checker */
		{ 1, 0x81, 0x80, 0x80 },	/* positive and negative */
		{ 1, 0x81, 0, 0xff },		/* positive and zero */
		{ 0x80, 0x80, 0, 0xff },	/* negative and zero */
	};
	int block = 64, bit, adj, sign, p, pattern, index;
	struct batch b;

	if (!batch_init(&b))
		return;
	snprintf(b.desc, sizeof(b.desc), "ld_imm64 magnitudes");
	for (bit = 0; bit <= 62; bit++)
		for (adj = -block / 2; adj < block / 2; adj++)
			for (sign = -1; sign <= 1; sign += 2)
				ld_imm64_case(&b, sign * ((1LL << bit) + adj));
	batch_flush(&b);

	for (p = 0; p < ARRAY_SIZE(bytes); p++) {
		__u32 rand = 1;

		snprintf(b.desc, sizeof(b.desc), "ld_imm64 byte pattern %d", p);
		for (pattern = 0; pattern < 256; pattern++) {
			__u64 imm = 0;

			for (index = 0; index < 8; index++) {
				int byte;

				if (pattern & (1 << index))
					byte = (bytes[p].base1 & bytes[p].mask1) |
					       (rand & ~bytes[p].mask1);
				else
					byte = (bytes[p].base2 & bytes[p].mask2) |
					       (rand & ~bytes[p].mask2);
				imm = (imm << 8) | (__u8)byte;
			}
			rand = rand * 1664525 + 1013904223;
			ld_imm64_case(&b, imm);
		}
		batch_flush(&b);
	}
	batch_fini(&b);
}

/* Conditional jumps with an immediate and with a register for all magnitudes */
static const int jmp_ops[] = {
	BPF_JSET, BPF_JEQ, BPF_JNE, BPF_JGT, BPF_JGE, BPF_JLT, BPF_JLE,
	BPF_JSGT, BPF_JSGE, BPF_JSLT, BPF_JSLE,
};

static bool jmp_match(__s64 v1, __s64 v2, __u8 op)
{
	switch (op) {
	case BPF_JSET:
		return !!(v1 & v2);
	case BPF_JEQ:
		return v1 == v2;
	case BPF_JNE:
		return v1 != v2;
	case BPF_JGT:
		return (__u64)v1 > (__u64)v2;
	case BPF_JGE:
		return (__u64)v1 >= (__u64)v2;
	case BPF_JLT:
		return (__u64)v1 < (__u64)v2;
	case BPF_JLE:
		return (__u64)v1 <= (__u64)v2;
	case BPF_JSGT:
		return v1 > v2;
	case BPF_JSGE:
		return v1 >= v2;
	case BPF_JSLT:
		return v1 < v2;
	case BPF_JSLE:
		return v1 <= v2;
	}
	return false;
}

struct jmp_arg {
	int op;
	bool is32;
	bool reg;
};

static void emit_jmp(struct batch *b, void *arg, __s64 dst, __s64 src)
{
	struct jmp_arg *a = arg;
	bool match;
	int k;

	if (a->is32)
		match = jmp_match((__s32)dst, (__s32)src, a->op);
	else
		match = jmp_match(dst, a->reg ? src : (__s32)src, a->op);

	k = batch_case(b, BPF_REG_3, dst, src);
	LD_IN(b, k, 0, BPF_REG_3, BPF_REG_1);
	if (a->reg)
		LD_IN(b, k, 1, BPF_REG_3, BPF_REG_2);
	EMIT(b, BPF_MOV64_IMM(BPF_REG_4, 1));
	if (a->reg && a->is32)
		EMIT(b, BPF_JMP32_REG(a->op, BPF_REG_1, BPF_REG_2, 1));
	else if (a->reg)
		EMIT(b, BPF_JMP_REG(a->op, BPF_REG_1, BPF_REG_2, 1));
	else if (a->is32)
		EMIT(b, BPF_JMP32_IMM(a->op, BPF_REG_1, src, 1));
	else
		EMIT(b, BPF_JMP_IMM(a->op, BPF_REG_1, src, 1));
	EMIT(b, BPF_MOV64_IMM(BPF_REG_4, 0));
	ST_OUT(b, k, 0, BPF_REG_3, BPF_REG_4);
	batch_expect(b, k, 0, match);
}

static void test_jmp_magnitudes(bool reg)
{
	struct batch b;
	int o, f;

	if (!batch_init(&b))
		return;
	for (f = 0; f < 2; f++) {
		for (o = 0; o < ARRAY_SIZE(jmp_ops); o++) {
			struct jmp_arg a = { jmp_ops[o], f, reg };

			snprintf(b.desc, sizeof(b.desc), "jmp%s_%s_%c magnitudes", f ? "32" : "",
				 jmp_name(a.op), reg ? 'x' : 'k');
			if (reg)
				fill_pattern(&b, &a, 64, 64, 0, 1, PATTERN_BLOCK2, emit_jmp);
			else
				fill_pattern(&b, &a, 64, 32, PATTERN_BLOCK1, 8, PATTERN_BLOCK2,
					     emit_jmp);
		}
	}
	batch_fini(&b);
}

static void test_jmp_imm(void)
{
	test_jmp_magnitudes(false);
}

static void test_jmp_reg(void)
{
	test_jmp_magnitudes(true);
}

/* A conditional jump on two registers, with the operands swapped for n == 1 */
static void jmp_reg_pair(struct batch *b, bool is32, int op, int rd, int rs, int n)
{
	const __u64 a = 0x7edcba9876543210ULL, c = 0x0123456789abcdefULL;
	__u64 v1 = n ? c : a, v2 = n ? a : c;
	int rc, rt, k;
	bool match;

	/* Two scratch registers besides the operands */
	rc = scratch_reg(rd, rs);
	for (rt = BPF_REG_1; rt < BPF_REG_9; rt++)
		if (rt != rd && rt != rs && rt != rc)
			break;
	if (rd == rs)
		v2 = v1;
	match = is32 ? jmp_match((__s32)v1, (__s32)v2, op) : jmp_match(v1, v2, op);

	k = batch_case(b, rc, v1, v2);
	LD_IN(b, k, 0, rc, rd);
	if (rs != rd)
		LD_IN(b, k, 1, rc, rs);
	EMIT(b, BPF_MOV64_IMM(rt, 1));
	if (is32)
		EMIT(b, BPF_JMP32_REG(op, rd, rs, 1));
	else
		EMIT(b, BPF_JMP_REG(op, rd, rs, 1));
	EMIT(b, BPF_MOV64_IMM(rt, 0));
	ST_OUT(b, k, 0, rc, rt);
	batch_expect(b, k, 0, match);
}

/* Conditional jumps for all combinations of operand registers, taken and not */
static void test_jmp_reg_pairs(void)
{
	struct batch b;
	int o, f, rd, rs, n;

	if (!batch_init(&b))
		return;
	for (f = 0; f < 2; f++) {
		for (o = 0; o < ARRAY_SIZE(jmp_ops); o++) {
			snprintf(b.desc, sizeof(b.desc), "jmp%s_%s_x register pairs",
				 f ? "32" : "", jmp_name(jmp_ops[o]));
			for (rd = BPF_REG_0; rd <= BPF_REG_9; rd++)
				for (rs = BPF_REG_0; rs <= BPF_REG_9; rs++)
					for (n = 0; n < 2; n++)
						jmp_reg_pair(&b, f, jmp_ops[o], rd, rs, n);
			batch_flush(&b);
		}
	}
	batch_fini(&b);
}

/* The offset of a jump instruction, which the 32-bit unconditional jump keeps in imm */
static void set_jmp_off(struct bpf_insn *insn, bool ja32, int off)
{
	if (ja32)
		insn->imm = off;
	else
		insn->off = off;
}

static struct bpf_insn jmp_insn(bool ja32)
{
	return ja32 ? BPF_RAW_INSN(BPF_JMP32 | BPF_JA, 0, 0, 0, 0) : BPF_JMP_IMM(BPF_JA, 0, 0, 0);
}

/*
 * Taken jumps over regions of doubling length, forward or backward, up to
 * the maximal offset of the jump: 16 bits, or 2^17 for the 32-bit
 * unconditional jump. The not taken path of a conditional jump and the
 * guard of an unconditional one lead to an exit with the region number, so
 * the verifier sees every instruction reachable and a jump landing anywhere
 * else than on its target fails the run.
 */
static void staggered_jumps(const struct bpf_insn *jmp, __s64 r1, __s64 r2, bool backward,
			    const char *desc)
{
	bool ja32 = BPF_CLASS(jmp->code) == BPF_JMP32 && BPF_OP(jmp->code) == BPF_JA;
	bool ja = BPF_OP(jmp->code) == BPF_JA;
	struct { __u64 r1, r2; } ctx = { r1, r2 };
	LIBBPF_OPTS(bpf_test_run_opts, opts,
		.ctx_in = &ctx,
		.ctx_size_in = sizeof(ctx),
	);
	int max = ja32 ? 1 << 17 : INT16_MAX;
	int len = 128 + 2 * max, i = 0, n = 0, gap, fd;
	struct bpf_insn *insns;

	insns = malloc(len * sizeof(*insns));
	if (!ASSERT_OK_PTR(insns, "insns"))
		return;
	insns[i++] = BPF_LDX_MEM(BPF_DW, BPF_REG_2, BPF_REG_1, 8);
	insns[i++] = BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_1, 0);
	insns[i++] = BPF_ALU64_IMM(BPF_MOV, BPF_REG_0, 0);
	for (gap = backward ? 4 : 2; gap <= max; gap = gap < max / 2 ? gap * 2 : max) {
		int k, a, t, x, g, j;

		if (backward) {
			/*
			 * Over the region to its guard and jump, which comes back
			 * to the start of the region, from where it leads on to
			 * the next one.
			 */
			a = i++;
			t = i;
			for (k = 0; k < gap - 3; k++)
				insns[i++] = BPF_ALU64_IMM(BPF_MOV, BPF_REG_3, 0);
			x = i++;
			g = i;
			if (ja)
				insns[i++] = BPF_JMP_IMM(BPF_JEQ, BPF_REG_2, 0, 1);
			j = i;
			insns[i] = *jmp;
			set_jmp_off(&insns[i++], ja32, t - (j + 1));
			insns[i++] = BPF_ALU64_IMM(BPF_MOV, BPF_REG_0, ++n);
			insns[i++] = BPF_EXIT_INSN();
			insns[a] = jmp_insn(ja32);
			set_jmp_off(&insns[a], ja32, g - (a + 1));
			insns[x] = jmp_insn(ja32);
			set_jmp_off(&insns[x], ja32, i - (x + 1));
		} else {
			/* An unconditional jump needs a way into its region */
			if (ja)
				insns[i++] = BPF_JMP_IMM(BPF_JEQ, BPF_REG_2, 0, 1);
			insns[i] = *jmp;
			set_jmp_off(&insns[i++], ja32, gap);
			insns[i++] = BPF_ALU64_IMM(BPF_MOV, BPF_REG_0, ++n);
			for (k = 0; k < gap - 2; k++)
				insns[i++] = BPF_ALU64_IMM(BPF_MOV, BPF_REG_3, 0);
			insns[i++] = BPF_EXIT_INSN();
		}
		if (gap == max)
			break;
	}
	insns[i++] = BPF_EXIT_INSN();
	if (!ASSERT_LE(i, len, "insn count"))
		goto out;

	fd = load_prog(insns, i, desc);
	if (fd >= 0) {
		if (ASSERT_OK(bpf_prog_test_run_opts(fd, &opts), desc))
			ASSERT_EQ(opts.retval, 0, desc);
		close(fd);
	}
out:
	free(insns);
}

static void test_staggered_jumps(void)
{
	/* A representative set, with operands which make the jump taken */
	static const struct {
		const char *desc;
		struct bpf_insn jmp;
		__s64 r1, r2;
	} kinds[] = {
		{ "staggered ja",
		  BPF_JMP_IMM(BPF_JA, 0, 0, 0), 0, 1 },
		{ "staggered gotol",
		  BPF_RAW_INSN(BPF_JMP32 | BPF_JA, 0, 0, 0, 0), 0, 1 },
		{ "staggered jmp_jeq_k",
		  BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 1234, 0), 1234, 0 },
		{ "staggered jmp_jne_x",
		  BPF_JMP_REG(BPF_JNE, BPF_REG_1, BPF_REG_2, 0), 4321, 1234 },
		{ "staggered jmp_jset_k",
		  BPF_JMP_IMM(BPF_JSET, BPF_REG_1, 0x82, 0), 0x86, 0 },
		{ "staggered jmp_jsgt_x",
		  BPF_JMP_REG(BPF_JSGT, BPF_REG_1, BPF_REG_2, 0), -1, -2 },
		{ "staggered jmp32_jlt_k",
		  BPF_JMP32_IMM(BPF_JLT, BPF_REG_1, 0x80000000, 0), 1234, 0 },
		{ "staggered jmp32_jsge_x",
		  BPF_JMP32_REG(BPF_JSGE, BPF_REG_1, BPF_REG_2, 0), -2, -2 },
	};
	char desc[64];
	int i, d;

	for (i = 0; i < ARRAY_SIZE(kinds); i++) {
		for (d = 0; d < 2; d++) {
			snprintf(desc, sizeof(desc), "%s %s", kinds[i].desc,
				 d ? "backward" : "forward");
			staggered_jumps(&kinds[i].jmp, kinds[i].r1, kinds[i].r2, d, desc);
		}
	}
}

/*
 * The pattern sweeps: one program per pattern and register choice, run
 * once per input with the operands in the ctx and the result handed back
 * through it.
 */
struct pat_ctx {
	__u64 in0;
	__u64 in1;
	__u64 res;
};

#define IN0	offsetof(struct pat_ctx, in0)
#define IN1	offsetof(struct pat_ctx, in1)
#define RES	offsetof(struct pat_ctx, res)

/* r6 holds the ctx */
static const int pat_regs[] = {
	BPF_REG_0, BPF_REG_1, BPF_REG_2, BPF_REG_3, BPF_REG_4,
	BPF_REG_5, BPF_REG_7, BPF_REG_8, BPF_REG_9,
};

/* The second register of a pattern: the same one, the next and a far one */
#define NR_PAIRS 3
static const int pair_off[NR_PAIRS] = { 0, 1, 5 };

static int pair_reg(int i, int k)
{
	return pat_regs[(i + pair_off[k]) % ARRAY_SIZE(pat_regs)];
}

static const __u64 inputs[] = {
	0, 0x1122334455667788ULL, 0xfedcba9876543210ULL, 0x80, 0xffffffffffffff7fULL,
};

struct prog {
	struct bpf_insn insns[16];
	int cnt;
};

static void prog_start(struct prog *p)
{
	p->cnt = 0;
	EMIT(p, BPF_MOV64_REG(BPF_REG_6, BPF_REG_1));
}

/* Hand the result in reg back and return */
static void prog_end(struct prog *p, int reg)
{
	EMIT(p, BPF_STX_MEM(BPF_DW, BPF_REG_6, reg, RES));
	EMIT(p, BPF_MOV64_IMM(BPF_REG_0, 0));
	EMIT(p, BPF_EXIT_INSN());
}

static int prog_load(struct prog *p, const char *desc)
{
	LIBBPF_OPTS(bpf_prog_load_opts, lopts,
		.prog_flags = BPF_F_SLEEPABLE,
		.log_buf = log,
		.log_size = sizeof(log),
	);
	int fd;

	fd = bpf_prog_load(BPF_PROG_TYPE_SYSCALL, NULL, "GPL", p->insns, p->cnt, &lopts);
	if (!ASSERT_GE(fd, 0, desc))
		fprintf(stderr, "%s", log);
	return fd;
}

static void prog_run(int fd, __u64 in0, __u64 in1, __u64 expected, const char *desc)
{
	struct pat_ctx ctx = { .in0 = in0, .in1 = in1 };
	LIBBPF_OPTS(bpf_test_run_opts, opts,
		.ctx_in = &ctx,
		.ctx_size_in = sizeof(ctx),
	);
	char buf[128];

	snprintf(buf, sizeof(buf), "%s in0=0x%llx in1=0x%llx", desc, in0, in1);
	if (ASSERT_OK(bpf_prog_test_run_opts(fd, &opts), buf))
		ASSERT_EQ(ctx.res, expected, buf);
}

static int size_bits(int size)
{
	switch (size) {
	case BPF_B:
		return 8;
	case BPF_H:
		return 16;
	case BPF_W:
		return 32;
	default:
		return 64;
	}
}

/* The first size bytes of v in memory, as a load of that size reads them */
static __u64 head_bytes(__u64 v, int size)
{
	int bits = size_bits(size);

	if (bits == 64)
		return v;
#if __BYTE_ORDER == __BIG_ENDIAN
	return v >> (64 - bits);
#else
	return v & ((1ULL << bits) - 1);
#endif
}

/* v with its first size bytes in memory replaced by those of w */
static __u64 head_merge(__u64 v, __u64 w, int size)
{
	int bits = size_bits(size);
	__u64 mask;

	if (bits == 64)
		return w;
	mask = (1ULL << bits) - 1;
#if __BYTE_ORDER == __BIG_ENDIAN
	return (v & ~(mask << (64 - bits))) | ((w & mask) << (64 - bits));
#else
	return (v & ~mask) | (w & mask);
#endif
}

static void test_ldst_zero_off(void)
{
	static const int sizes[] = { BPF_B, BPF_H, BPF_W, BPF_DW };
	static const int aops[] = {
		BPF_ADD, BPF_ADD | BPF_FETCH, BPF_OR, BPF_XCHG, BPF_CMPXCHG,
	};
	static const __s32 imms[] = { 0x5a, -1 };
	struct prog p;
	char desc[64];
	int b, k, s, j, fd;

	for (b = 0; b < ARRAY_SIZE(pat_regs); b++) {
		int rb = pat_regs[b];

		for (s = 0; s < ARRAY_SIZE(sizes); s++) {
			int size = sizes[s];

			for (k = 0; k < NR_PAIRS; k++) {
				int rd = pair_reg(b, k);

				/* Load with a zero offset, also into the base */
				prog_start(&p);
				EMIT(&p, BPF_MOV64_REG(rb, BPF_REG_6));
				EMIT(&p, BPF_LDX_MEM(size, rd, rb, 0));
				prog_end(&p, rd);
				snprintf(desc, sizeof(desc), "ldx size=%d base=r%d dst=r%d",
					 size, rb, rd);
				fd = prog_load(&p, desc);
				if (fd < 0)
					return;
				for (j = 0; j < ARRAY_SIZE(inputs); j++)
					prog_run(fd, inputs[j], 0,
						 head_bytes(inputs[j], size), desc);
				close(fd);

				/* Sign extending load with a zero offset from the stack */
				if (size != BPF_DW) {
					prog_start(&p);
					EMIT(&p, BPF_LDX_MEM(BPF_DW, rd, BPF_REG_6, IN0));
					EMIT(&p, BPF_STX_MEM(BPF_DW, BPF_REG_10, rd, -8));
					EMIT(&p, BPF_MOV64_REG(rb, BPF_REG_10));
					EMIT(&p, BPF_ALU64_IMM(BPF_ADD, rb, -8));
					EMIT(&p, BPF_RAW_INSN(BPF_LDX | BPF_MEMSX | size,
							      rd, rb, 0, 0));
					prog_end(&p, rd);
					snprintf(desc, sizeof(desc),
						 "ldsx size=%d base=r%d dst=r%d", size, rb, rd);
					fd = prog_load(&p, desc);
					if (fd < 0)
						return;
					for (j = 0; j < ARRAY_SIZE(inputs); j++) {
						__s64 v = head_bytes(inputs[j], size);

						v = size == BPF_B ? (__s8)v :
						    size == BPF_H ? (__s16)v : (__s32)v;
						prog_run(fd, inputs[j], 0, v, desc);
					}
					close(fd);
				}

				if (rd == rb)
					continue;
				/* Store with a zero offset over in0 */
				prog_start(&p);
				EMIT(&p, BPF_MOV64_REG(rb, BPF_REG_6));
				EMIT(&p, BPF_LDX_MEM(BPF_DW, rd, BPF_REG_6, IN1));
				EMIT(&p, BPF_STX_MEM(size, rb, rd, 0));
				EMIT(&p, BPF_LDX_MEM(BPF_DW, rd, BPF_REG_6, IN0));
				prog_end(&p, rd);
				snprintf(desc, sizeof(desc), "stx size=%d base=r%d src=r%d",
					 size, rb, rd);
				fd = prog_load(&p, desc);
				if (fd < 0)
					return;
				for (j = 0; j < ARRAY_SIZE(inputs); j++) {
					__u64 in1 = inputs[(j + 1) % ARRAY_SIZE(inputs)];

					prog_run(fd, inputs[j], in1,
						 head_merge(inputs[j], in1, size), desc);
				}
				close(fd);
			}

			/* Store an immediate with a zero offset into a zeroed stack slot */
			for (k = 0; k < ARRAY_SIZE(imms); k++) {
				int rd = pair_reg(b, 1);

				prog_start(&p);
				EMIT(&p, BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0));
				EMIT(&p, BPF_MOV64_REG(rb, BPF_REG_10));
				EMIT(&p, BPF_ALU64_IMM(BPF_ADD, rb, -8));
				EMIT(&p, BPF_ST_MEM(size, rb, 0, imms[k]));
				EMIT(&p, BPF_LDX_MEM(BPF_DW, rd, BPF_REG_10, -8));
				prog_end(&p, rd);
				snprintf(desc, sizeof(desc), "st size=%d base=r%d imm=%d",
					 size, rb, imms[k]);
				fd = prog_load(&p, desc);
				if (fd < 0)
					return;
				prog_run(fd, 0, 0,
					 head_merge(0, (__u64)(__s64)imms[k], size), desc);
				close(fd);
			}

			/* Atomics with a zero offset on a stack slot holding in0 */
			for (k = 0; k < ARRAY_SIZE(aops); k++) {
				int rd = pair_reg(b, 1), op = aops[k];

				if (size == BPF_B || size == BPF_H)
					break;
				/* cmpxchg compares with r0 */
				if (op == BPF_CMPXCHG && (rb == BPF_REG_0 || rd == BPF_REG_0))
					continue;
				prog_start(&p);
				EMIT(&p, BPF_LDX_MEM(BPF_DW, rd, BPF_REG_6, IN0));
				EMIT(&p, BPF_STX_MEM(BPF_DW, BPF_REG_10, rd, -8));
				EMIT(&p, BPF_LDX_MEM(BPF_DW, rd, BPF_REG_6, IN1));
				EMIT(&p, BPF_MOV64_REG(rb, BPF_REG_10));
				EMIT(&p, BPF_ALU64_IMM(BPF_ADD, rb, -8));
				if (op == BPF_CMPXCHG)
					EMIT(&p, BPF_LDX_MEM(size, BPF_REG_0, BPF_REG_10, -8));
				EMIT(&p, BPF_ATOMIC_OP(size, op, rb, rd, 0));
				EMIT(&p, BPF_LDX_MEM(BPF_DW, rd, BPF_REG_10, -8));
				prog_end(&p, rd);
				snprintf(desc, sizeof(desc), "atomic 0x%x size=%d base=r%d src=r%d",
					 op, size, rb, rd);
				fd = prog_load(&p, desc);
				if (fd < 0)
					return;
				for (j = 0; j < ARRAY_SIZE(inputs); j++) {
					__u64 in1 = inputs[(j + 1) % ARRAY_SIZE(inputs)];
					__u64 v = head_bytes(inputs[j], size);
					/* The source is the low half, whichever the byte order */
					__u64 w = size == BPF_W ? (__u32)in1 : in1;
					__u64 r = BPF_OP(op) == BPF_ADD ? v + w :
						  BPF_OP(op) == BPF_OR ? v | w : w;

					prog_run(fd, inputs[j], in1,
						 head_merge(inputs[j], r, size), desc);
				}
				close(fd);
			}
		}
	}
}

/*
 * Loads, stores and stores of an immediate with the offsets around the
 * limits of the short displacement forms, negative, large and unaligned
 * ones, with every register as base. The memory is an array map value with
 * the base register pointing at its middle, the loads hand their results
 * back through a second value and the stores are checked against a copy
 * kept here. The unaligned offsets are left out where the verifier insists
 * on aligned accesses.
 */
#define MEM_SIZE	65536
#define MEM_BASE	(MEM_SIZE / 2)
#define MEM_KEY		0
#define RES_KEY		1
#define SLOT_KEY	-4	/* stack slots: map key, results and base pointer */
#define SLOT_RES	-16
#define SLOT_BASE	-24
#define STORE_VAL	0x8899aabbccddeeffULL

struct off_prog {
	struct bpf_insn insns[128];
	int cnt;
};

/* The map holding the memory and the results, a copy of the memory and a buffer */
struct off_mem {
	int map_fd;
	__u8 *mem, *exp, *res;
};

enum { K_LDX, K_LDSX, K_STX, K_ST };

/* The offsets for an access of size, aligned unless unaligned ones are ok */
static int offsets_for(int size, bool unaligned, int *off)
{
	int n = size_bits(size) / 8, cnt = 0;

	off[cnt++] = n;
	off[cnt++] = 128 - n;
	off[cnt++] = 128;
	off[cnt++] = 256 - n;
	off[cnt++] = 256;
	off[cnt++] = 4096;
	off[cnt++] = MEM_BASE - n;
	off[cnt++] = -n;
	off[cnt++] = -128;
	off[cnt++] = -128 - n;
	off[cnt++] = -256;
	off[cnt++] = -4096;
	off[cnt++] = -MEM_BASE;
	if (n > 1 && unaligned) {
		off[cnt++] = 13;
		off[cnt++] = 127;
		off[cnt++] = -13;
		off[cnt++] = -129;
	}
	return cnt;
}

/* The bytes a store of size writes from the register value v */
static void store_bytes(__u8 *dst, __u64 v, int size)
{
	int n = size_bits(size) / 8;

#if __BYTE_ORDER == __BIG_ENDIAN
	memcpy(dst, (__u8 *)&v + 8 - n, n);
#else
	memcpy(dst, &v, n);
#endif
}

/* The value a load of size reads from the bytes at src */
static __u64 load_bytes(const __u8 *src, int size)
{
	__u64 v = 0;

	memcpy(&v, src, size_bits(size) / 8);
	return head_bytes(v, size);
}

/* A register that is neither of the two given ones */
static int other_reg(int r1, int r2)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(pat_regs); i++)
		if (pat_regs[i] != r1 && pat_regs[i] != r2)
			return pat_regs[i];
	return BPF_REG_9;
}

/* Look up the value of key into r0, exit with 1 when the lookup fails */
static void off_prog_lookup(struct off_prog *p, int map_fd, int key)
{
	EMIT(p, BPF_ST_MEM(BPF_W, BPF_REG_10, SLOT_KEY, key));
	EMIT(p, BPF_RAW_INSN(BPF_LD | BPF_DW | BPF_IMM, BPF_REG_1, BPF_PSEUDO_MAP_FD, 0, map_fd));
	EMIT(p, BPF_RAW_INSN(0, 0, 0, 0, 0));
	EMIT(p, BPF_MOV64_REG(BPF_REG_2, BPF_REG_10));
	EMIT(p, BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, SLOT_KEY));
	EMIT(p, BPF_EMIT_CALL(BPF_FUNC_map_lookup_elem));
	EMIT(p, BPF_JMP_IMM(BPF_JNE, BPF_REG_0, 0, 2));
	EMIT(p, BPF_MOV64_IMM(BPF_REG_0, 1));
	EMIT(p, BPF_EXIT_INSN());
}

/* rb points at the middle of the memory, the results value is spilled */
static void off_prog_start(struct off_prog *p, int map_fd, int rb)
{
	p->cnt = 0;
	off_prog_lookup(p, map_fd, RES_KEY);
	EMIT(p, BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_0, SLOT_RES));
	off_prog_lookup(p, map_fd, MEM_KEY);
	EMIT(p, BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, MEM_BASE));
	EMIT(p, BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_0, SLOT_BASE));
	if (rb != BPF_REG_0)
		EMIT(p, BPF_MOV64_REG(rb, BPF_REG_0));
}

static int off_prog_load(struct off_prog *p, const char *desc, bool quiet)
{
	LIBBPF_OPTS(bpf_prog_load_opts, lopts,
		.prog_flags = BPF_F_SLEEPABLE,
		.log_buf = log,
		.log_size = sizeof(log),
	);
	int fd;

	EMIT(p, BPF_MOV64_IMM(BPF_REG_0, 0));
	EMIT(p, BPF_EXIT_INSN());
	fd = bpf_prog_load(BPF_PROG_TYPE_SYSCALL, NULL, "GPL", p->insns, p->cnt, &lopts);
	if (quiet)
		return fd;
	if (!ASSERT_GE(fd, 0, desc))
		fprintf(stderr, "%s", log);
	return fd;
}

static int off_prog_run(int fd, const char *desc)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	int err;

	err = bpf_prog_test_run_opts(fd, &opts);
	close(fd);
	if (!ASSERT_OK(err, desc) || !ASSERT_EQ(opts.retval, 0, desc))
		return -1;
	return 0;
}

/* Whether the verifier accepts a halfword load from an odd offset */
static bool unaligned_ok(int map_fd)
{
	struct off_prog p;
	int fd;

	off_prog_start(&p, map_fd, BPF_REG_1);
	EMIT(&p, BPF_LDX_MEM(BPF_H, BPF_REG_0, BPF_REG_1, 1));
	fd = off_prog_load(&p, "unaligned probe", true);
	if (fd < 0)
		return false;
	close(fd);
	return true;
}

/* One program loading from all the offsets of size through rb into rd */
static void load_offsets(struct off_mem *m, int kind, int size, int rb, int rd,
			 const int *off, int nr)
{
	__u32 mem_key = MEM_KEY, res_key = RES_KEY;
	int rs = other_reg(rb, rd), fd, i;
	__u64 *res = (__u64 *)m->res;
	int map_fd = m->map_fd;
	struct off_prog p;
	char desc[64];

	off_prog_start(&p, map_fd, rb);
	for (i = 0; i < nr; i++) {
		if (rd == rb)
			EMIT(&p, BPF_LDX_MEM(BPF_DW, rb, BPF_REG_10, SLOT_BASE));
		if (kind == K_LDX)
			EMIT(&p, BPF_LDX_MEM(size, rd, rb, off[i]));
		else
			EMIT(&p, BPF_RAW_INSN(BPF_LDX | BPF_MEMSX | size, rd, rb, off[i], 0));
		EMIT(&p, BPF_LDX_MEM(BPF_DW, rs, BPF_REG_10, SLOT_RES));
		EMIT(&p, BPF_STX_MEM(BPF_DW, rs, rd, i * 8));
	}
	snprintf(desc, sizeof(desc), "%s size=%d base=r%d dst=r%d",
		 kind == K_LDX ? "ldx" : "ldsx", size, rb, rd);
	if (!ASSERT_OK(bpf_map_update_elem(map_fd, &mem_key, m->mem, 0), desc))
		return;
	fd = off_prog_load(&p, desc, false);
	if (fd < 0)
		return;
	if (off_prog_run(fd, desc) || !ASSERT_OK(bpf_map_lookup_elem(map_fd, &res_key, res), desc))
		return;
	for (i = 0; i < nr; i++) {
		__u64 v = load_bytes(m->mem + MEM_BASE + off[i], size);

		if (kind == K_LDSX)
			v = size == BPF_B ? (__s8)v : size == BPF_H ? (__s16)v : (__s32)v;
		if (res[i] != v) {
			snprintf(desc, sizeof(desc), "%s size=%d base=r%d dst=r%d off=%d",
				 kind == K_LDX ? "ldx" : "ldsx", size, rb, rd, off[i]);
			ASSERT_EQ(res[i], v, desc);
		}
	}
}

/* One program storing rd, or the immediate imm when rd < 0, at all the offsets */
static void store_offsets(struct off_mem *m, int size, int rb, int rd, __s32 imm,
			  const int *off, int nr)
{
	__u64 val = rd >= 0 ? STORE_VAL : (__u64)(__s64)imm;
	__u8 *mem = m->mem, *exp = m->exp, *res = m->res;
	__u32 mem_key = MEM_KEY;
	int map_fd = m->map_fd;
	char desc[64], buf[96];
	struct off_prog p;
	int fd, i;

	off_prog_start(&p, map_fd, rb);
	if (rd >= 0) {
		EMIT(&p, BPF_RAW_INSN(BPF_LD | BPF_DW | BPF_IMM, rd, 0, 0, (__u32)STORE_VAL));
		EMIT(&p, BPF_RAW_INSN(0, 0, 0, 0, STORE_VAL >> 32));
	}
	memcpy(exp, mem, MEM_SIZE);
	for (i = 0; i < nr; i++) {
		if (rd >= 0)
			EMIT(&p, BPF_STX_MEM(size, rb, rd, off[i]));
		else
			EMIT(&p, BPF_ST_MEM(size, rb, off[i], imm));
		store_bytes(exp + MEM_BASE + off[i], val, size);
	}
	if (rd >= 0)
		snprintf(desc, sizeof(desc), "stx size=%d base=r%d src=r%d", size, rb, rd);
	else
		snprintf(desc, sizeof(desc), "st size=%d base=r%d imm=%d", size, rb, imm);
	if (!ASSERT_OK(bpf_map_update_elem(map_fd, &mem_key, mem, 0), desc))
		return;
	fd = off_prog_load(&p, desc, false);
	if (fd < 0)
		return;
	if (off_prog_run(fd, desc) || !ASSERT_OK(bpf_map_lookup_elem(map_fd, &mem_key, res), desc))
		return;
	for (i = 0; i < MEM_SIZE && res[i] == exp[i]; i++)
		;
	if (i < MEM_SIZE) {
		snprintf(buf, sizeof(buf), "%s: byte at offset %d", desc, i - MEM_BASE);
		ASSERT_EQ(res[i], exp[i], buf);
	}
}

static void test_ldst_offsets(void)
{
	static const int sizes[] = { BPF_B, BPF_H, BPF_W, BPF_DW };
	static const __s32 imms[] = { -1, 0x7fffffff, -0x7fffffff - 1 };
	int b, k, s, i, nr, off[32];
	struct off_mem m = {};
	bool unaligned;

	m.map_fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, "ldst_mem", sizeof(__u32), MEM_SIZE, 2, NULL);
	if (!ASSERT_GE(m.map_fd, 0, "ldst_mem map"))
		return;
	m.mem = malloc(MEM_SIZE);
	m.exp = malloc(MEM_SIZE);
	m.res = malloc(MEM_SIZE);
	if (!ASSERT_OK_PTR(m.mem, "mem") || !ASSERT_OK_PTR(m.exp, "exp") ||
	    !ASSERT_OK_PTR(m.res, "res"))
		goto out;
	for (i = 0; i < MEM_SIZE; i++)
		m.mem[i] = (i * 7 + 3) ^ (i >> 8);
	unaligned = unaligned_ok(m.map_fd);

	for (b = 0; b < ARRAY_SIZE(pat_regs); b++) {
		for (s = 0; s < ARRAY_SIZE(sizes); s++) {
			int rb = pat_regs[b], size = sizes[s];

			nr = offsets_for(size, unaligned, off);
			/* Loads and sign extending loads, also into the base */
			for (k = 0; k < NR_PAIRS; k++) {
				int rd = pair_reg(b, k);

				load_offsets(&m, K_LDX, size, rb, rd, off, nr);
				if (size != BPF_DW)
					load_offsets(&m, K_LDSX, size, rb, rd, off, nr);
			}
			/* Stores of a register and of an immediate */
			for (k = 1; k < NR_PAIRS; k++)
				store_offsets(&m, size, rb, pair_reg(b, k), 0, off, nr);
			for (k = 0; k < ARRAY_SIZE(imms); k++)
				store_offsets(&m, size, rb, -1, imms[k], off, nr);
		}
	}
out:
	free(m.mem);
	free(m.exp);
	free(m.res);
	close(m.map_fd);
}

static void test_movsx(void)
{
	static const struct {
		bool is64;
		int off;
	} ops[] = {
		{ true, 8 }, { true, 16 }, { true, 32 }, { false, 8 }, { false, 16 },
	};
	struct prog p;
	char desc[64];
	int s, k, o, j, fd;

	for (s = 0; s < ARRAY_SIZE(pat_regs); s++) {
		int rs = pat_regs[s];

		for (k = 0; k < NR_PAIRS; k++) {
			int rd = pair_reg(s, k);

			for (o = 0; o < ARRAY_SIZE(ops); o++) {
				prog_start(&p);
				EMIT(&p, BPF_LDX_MEM(BPF_DW, rs, BPF_REG_6, IN0));
				if (ops[o].is64)
					EMIT(&p, BPF_MOVSX64_REG(rd, rs, ops[o].off));
				else
					EMIT(&p, BPF_MOVSX32_REG(rd, rs, ops[o].off));
				prog_end(&p, rd);
				snprintf(desc, sizeof(desc), "movsx%d off=%d dst=r%d src=r%d",
					 ops[o].is64 ? 64 : 32, ops[o].off, rd, rs);
				fd = prog_load(&p, desc);
				if (fd < 0)
					return;
				for (j = 0; j < ARRAY_SIZE(inputs); j++) {
					__s64 v = inputs[j];

					if (ops[o].off == 8)
						v = (__s8)v;
					else if (ops[o].off == 16)
						v = (__s16)v;
					else
						v = (__s32)v;
					prog_run(fd, inputs[j], 0,
						 ops[o].is64 ? (__u64)v : (__u32)v, desc);
				}
				close(fd);
			}
		}
	}
}

/* Branch on dst and imm, with 1 in res when taken and 0 otherwise */
static void jmp_prog(struct prog *p, bool is64, int op, int d, __s32 imm)
{
	int rd = pat_regs[d], rt = pair_reg(d, 1);

	prog_start(p);
	EMIT(p, BPF_LDX_MEM(BPF_DW, rd, BPF_REG_6, IN0));
	EMIT(p, BPF_MOV64_IMM(rt, 1));
	if (is64)
		EMIT(p, BPF_JMP_IMM(op, rd, imm, 1));
	else
		EMIT(p, BPF_JMP32_IMM(op, rd, imm, 1));
	EMIT(p, BPF_MOV64_IMM(rt, 0));
	prog_end(p, rt);
}

static void test_jset_imm(void)
{
	static const __s32 masks[] = {
		0x1, 0x40, 0x80, 0xff, 0x100, 0x7fff, 0x7fffffff, -1, -0x7fffffff - 1,
	};
	struct prog p;
	char desc[64];
	int d, m, j, fd;
	bool is64;

	for (d = 0; d < ARRAY_SIZE(pat_regs); d++) {
		int rd = pat_regs[d];

		for (is64 = false; ; is64 = true) {
			for (m = 0; m < ARRAY_SIZE(masks); m++) {
				jmp_prog(&p, is64, BPF_JSET, d, masks[m]);
				snprintf(desc, sizeof(desc), "jset%d dst=r%d mask=0x%x",
					 is64 ? 64 : 32, rd, masks[m]);
				fd = prog_load(&p, desc);
				if (fd < 0)
					return;
				for (j = 0; j < ARRAY_SIZE(inputs); j++) {
					__u64 v = inputs[j];
					bool taken;

					if (is64)
						taken = v & (__u64)(__s64)masks[m];
					else
						taken = (__u32)v & (__u32)masks[m];
					prog_run(fd, v, 0, taken, desc);
				}
				close(fd);
			}
			if (is64)
				break;
		}
	}
}

static bool cmp_taken(int op, bool is64, __u64 v, __u64 c)
{
	__s64 sv = is64 ? (__s64)v : (__s32)v, sc = is64 ? (__s64)c : (__s32)c;

	switch (op) {
	case BPF_JEQ:
		return v == c;
	case BPF_JNE:
		return v != c;
	case BPF_JGT:
		return v > c;
	case BPF_JGE:
		return v >= c;
	case BPF_JLT:
		return v < c;
	case BPF_JLE:
		return v <= c;
	case BPF_JSGT:
		return sv > sc;
	case BPF_JSGE:
		return sv >= sc;
	case BPF_JSLT:
		return sv < sc;
	default:
		return sv <= sc;
	}
}

static void test_cmp_imm(void)
{
	static const __s32 imms[] = {
		1, -1, 0x7f, 0x80, -0x80, -0x81, 0x1000, 0x7fffffff,
	};
	static const int ops[] = {
		BPF_JEQ, BPF_JNE, BPF_JGT, BPF_JGE, BPF_JLT, BPF_JLE,
		BPF_JSGT, BPF_JSGE, BPF_JSLT, BPF_JSLE,
	};
	struct prog p;
	char desc[64];
	int d, o, m, j, fd;
	bool is64;

	for (d = 0; d < ARRAY_SIZE(pat_regs); d++) {
		int rd = pat_regs[d];

		for (is64 = false; ; is64 = true) {
			for (o = 0; o < ARRAY_SIZE(ops); o++) {
				for (m = 0; m < ARRAY_SIZE(imms); m++) {
					jmp_prog(&p, is64, ops[o], d, imms[m]);
					snprintf(desc, sizeof(desc), "jmp%d op=0x%x dst=r%d imm=%d",
						 is64 ? 64 : 32, ops[o], rd, imms[m]);
					fd = prog_load(&p, desc);
					if (fd < 0)
						return;
					for (j = 0; j < ARRAY_SIZE(inputs); j++) {
						__u64 v = is64 ? inputs[j] : (__u32)inputs[j];
						__u64 c = is64 ? (__u64)(__s64)imms[m] :
							  (__u32)imms[m];
						__u64 taken = cmp_taken(ops[o], is64, v, c);

						prog_run(fd, inputs[j], 0, taken, desc);
					}
					close(fd);
				}
			}
			if (is64)
				break;
		}
	}
}

static __u64 alu_model(int op, __u64 v, __u64 c)
{
	switch (op) {
	case BPF_ADD:
		return v + c;
	case BPF_SUB:
		return v - c;
	case BPF_AND:
		return v & c;
	case BPF_OR:
		return v | c;
	case BPF_XOR:
		return v ^ c;
	default:
		return v * c;
	}
}

static void test_alu_consts(void)
{
	static const struct {
		int op;
		__s32 imm;
	} ops[] = {
		{ BPF_ADD, 1 }, { BPF_ADD, -1 }, { BPF_SUB, 1 }, { BPF_SUB, -1 },
		{ BPF_ADD, 128 }, { BPF_SUB, 128 }, { BPF_ADD, -128 },
		{ BPF_AND, 0xff }, { BPF_AND, 0xffff }, { BPF_AND, 0x7f },
		{ BPF_AND, 0x3ff }, { BPF_AND, 0x7fffffff },
		{ BPF_OR, 0xff }, { BPF_XOR, -1 }, { BPF_XOR, 0x80 },
		{ BPF_MUL, 2 }, { BPF_MUL, 3 }, { BPF_MUL, 4 }, { BPF_MUL, 5 },
		{ BPF_MUL, 7 }, { BPF_MUL, 8 }, { BPF_MUL, 9 }, { BPF_MUL, 16 },
		{ BPF_MUL, 0x40000000 }, { BPF_MUL, -3 }, { BPF_MUL, -0x7fffffff - 1 },
		{ BPF_MUL, 0 }, { BPF_MUL, 1 }, { BPF_MUL, -1 },
	};
	struct prog p;
	char desc[64];
	int d, o, j, fd;
	bool is64;

	for (d = 0; d < ARRAY_SIZE(pat_regs); d++) {
		int rd = pat_regs[d];

		for (is64 = false; ; is64 = true) {
			for (o = 0; o < ARRAY_SIZE(ops); o++) {
				prog_start(&p);
				EMIT(&p, BPF_LDX_MEM(BPF_DW, rd, BPF_REG_6, IN0));
				if (is64)
					EMIT(&p, BPF_ALU64_IMM(ops[o].op, rd, ops[o].imm));
				else
					EMIT(&p, BPF_ALU32_IMM(ops[o].op, rd, ops[o].imm));
				prog_end(&p, rd);
				snprintf(desc, sizeof(desc), "alu%d op=0x%x dst=r%d imm=%d",
					 is64 ? 64 : 32, ops[o].op, rd, ops[o].imm);
				fd = prog_load(&p, desc);
				if (fd < 0)
					return;
				for (j = 0; j < ARRAY_SIZE(inputs); j++) {
					__u64 v = is64 ? inputs[j] : (__u32)inputs[j];
					__u64 c = is64 ? (__u64)(__s64)ops[o].imm :
						  (__u32)ops[o].imm;
					__u64 r = alu_model(ops[o].op, v, c);

					prog_run(fd, inputs[j], 0, is64 ? r : (__u32)r, desc);
				}
				close(fd);
			}
			if (is64)
				break;
		}
	}
}

static void test_bswap(void)
{
	static const int widths[] = { 16, 32, 64 };
	struct prog p;
	char desc[64];
	int d, w, o, j, fd;

	for (d = 0; d < ARRAY_SIZE(pat_regs); d++) {
		int rd = pat_regs[d];

		for (w = 0; w < ARRAY_SIZE(widths); w++) {
			int bits = widths[w];

			/* To big endian, to little endian, and the unconditional swap */
			for (o = 0; o < 3; o++) {
				prog_start(&p);
				EMIT(&p, BPF_LDX_MEM(BPF_DW, rd, BPF_REG_6, IN0));
				if (o == 0)
					EMIT(&p, BPF_ENDIAN(BPF_TO_BE, rd, bits));
				else if (o == 1)
					EMIT(&p, BPF_ENDIAN(BPF_TO_LE, rd, bits));
				else
					EMIT(&p, BPF_RAW_INSN(BPF_ALU64 | BPF_END | BPF_TO_LE,
							      rd, 0, 0, bits));
				prog_end(&p, rd);
				snprintf(desc, sizeof(desc), "%s%d dst=r%d",
					 o == 0 ? "be" : o == 1 ? "le" : "bswap", bits, rd);
				fd = prog_load(&p, desc);
				if (fd < 0)
					return;
				for (j = 0; j < ARRAY_SIZE(inputs); j++) {
					__u64 v = inputs[j], r;

					if (bits == 16)
						r = o == 0 ? htobe16(v) : o == 1 ? htole16(v) :
						    __builtin_bswap16(v);
					else if (bits == 32)
						r = o == 0 ? htobe32(v) : o == 1 ? htole32(v) :
						    __builtin_bswap32(v);
					else
						r = o == 0 ? htobe64(v) : o == 1 ? htole64(v) :
						    __builtin_bswap64(v);
					prog_run(fd, v, 0, r, desc);
				}
				close(fd);
			}
		}
	}
}

static void test_probe_mem(void)
{
	int kfunc_id, type_id, b, k, j, fd;
	struct btf *btf;
	struct prog p;
	char desc[64];

	btf = btf__load_vmlinux_btf();
	if (!ASSERT_OK_PTR(btf, "vmlinux btf"))
		return;
	kfunc_id = btf__find_by_name_kind(btf, "bpf_rdonly_cast", BTF_KIND_FUNC);
	type_id = btf__find_by_name_kind(btf, "timespec64", BTF_KIND_STRUCT);
	btf__free(btf);
	if (!ASSERT_GT(kfunc_id, 0, "bpf_rdonly_cast id") ||
	    !ASSERT_GT(type_id, 0, "timespec64 id"))
		return;

	/* The kernel copy of the ctx through an untrusted pointer in every register */
	for (b = 0; b < ARRAY_SIZE(pat_regs); b++) {
		int rb = pat_regs[b];

		for (k = 0; k < NR_PAIRS; k++) {
			int rd = pair_reg(b, k);

			prog_start(&p);
			EMIT(&p, BPF_MOV64_REG(BPF_REG_1, BPF_REG_6));
			EMIT(&p, BPF_MOV32_IMM(BPF_REG_2, type_id));
			EMIT(&p, BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, BPF_PSEUDO_KFUNC_CALL,
					      0, kfunc_id));
			if (rb != BPF_REG_0)
				EMIT(&p, BPF_MOV64_REG(rb, BPF_REG_0));
			EMIT(&p, BPF_LDX_MEM(BPF_DW, rd, rb, 0));
			prog_end(&p, rd);
			snprintf(desc, sizeof(desc), "probe_mem base=r%d dst=r%d", rb, rd);
			fd = prog_load(&p, desc);
			if (fd < 0)
				return;
			for (j = 0; j < ARRAY_SIZE(inputs); j++)
				prog_run(fd, inputs[j], 0, inputs[j], desc);
			close(fd);
		}
	}

	/* NULL and a user address are not touched and read as zero */
	for (j = 0; j < 2; j++) {
		__u64 addr = j ? 0x00007ffffffff000ULL : 0;

		prog_start(&p);
		EMIT(&p, BPF_RAW_INSN(BPF_LD | BPF_DW | BPF_IMM, BPF_REG_1, 0, 0, (__u32)addr));
		EMIT(&p, BPF_RAW_INSN(0, 0, 0, 0, addr >> 32));
		EMIT(&p, BPF_MOV32_IMM(BPF_REG_2, type_id));
		EMIT(&p, BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, BPF_PSEUDO_KFUNC_CALL,
				      0, kfunc_id));
		EMIT(&p, BPF_LDX_MEM(BPF_DW, BPF_REG_3, BPF_REG_0, 0));
		prog_end(&p, BPF_REG_3);
		snprintf(desc, sizeof(desc), "probe_mem addr=0x%llx", addr);
		fd = prog_load(&p, desc);
		if (fd < 0)
			return;
		prog_run(fd, 0, 0, 0, desc);
		close(fd);
	}
}

/* Whether the JIT supports load-acquire and store-release */
static bool acq_rel_ok(void)
{
	LIBBPF_OPTS(bpf_prog_load_opts, lopts, .prog_flags = BPF_F_SLEEPABLE);
	struct prog p;
	int fd;

	prog_start(&p);
	EMIT(&p, BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0));
	EMIT(&p, BPF_ATOMIC_OP(BPF_DW, BPF_LOAD_ACQ, BPF_REG_0, BPF_REG_10, -8));
	prog_end(&p, BPF_REG_0);
	fd = bpf_prog_load(BPF_PROG_TYPE_SYSCALL, NULL, "GPL", p.insns, p.cnt, &lopts);
	if (fd < 0)
		return false;
	close(fd);
	return true;
}

/*
 * Load-acquire and store-release of every size with every register as
 * base, on a stack slot holding in0: the loads zero extend like the plain
 * loads and the stores replace the first bytes of the slot like the plain
 * stores.
 */
static void test_acq_rel(void)
{
	static const int sizes[] = { BPF_B, BPF_H, BPF_W, BPF_DW };
	struct prog p;
	char desc[64];
	int b, k, s, j, fd;

	if (!acq_rel_ok()) {
		test__skip();
		return;
	}
	for (b = 0; b < ARRAY_SIZE(pat_regs); b++) {
		int rb = pat_regs[b];

		for (s = 0; s < ARRAY_SIZE(sizes); s++) {
			int size = sizes[s];

			for (k = 0; k < NR_PAIRS; k++) {
				int rd = pair_reg(b, k);

				/* Load-acquire from the slot, also into the base */
				prog_start(&p);
				EMIT(&p, BPF_LDX_MEM(BPF_DW, rd, BPF_REG_6, IN0));
				EMIT(&p, BPF_STX_MEM(BPF_DW, BPF_REG_10, rd, -8));
				EMIT(&p, BPF_MOV64_REG(rb, BPF_REG_10));
				EMIT(&p, BPF_ALU64_IMM(BPF_ADD, rb, -8));
				EMIT(&p, BPF_ATOMIC_OP(size, BPF_LOAD_ACQ, rd, rb, 0));
				prog_end(&p, rd);
				snprintf(desc, sizeof(desc), "load_acq size=%d base=r%d dst=r%d",
					 size, rb, rd);
				fd = prog_load(&p, desc);
				if (fd < 0)
					return;
				for (j = 0; j < ARRAY_SIZE(inputs); j++)
					prog_run(fd, inputs[j], 0,
						 head_bytes(inputs[j], size), desc);
				close(fd);

				if (rd == rb)
					continue;
				/* Store-release of in1 over the slot holding in0 */
				prog_start(&p);
				EMIT(&p, BPF_LDX_MEM(BPF_DW, rd, BPF_REG_6, IN0));
				EMIT(&p, BPF_STX_MEM(BPF_DW, BPF_REG_10, rd, -8));
				EMIT(&p, BPF_LDX_MEM(BPF_DW, rd, BPF_REG_6, IN1));
				EMIT(&p, BPF_MOV64_REG(rb, BPF_REG_10));
				EMIT(&p, BPF_ALU64_IMM(BPF_ADD, rb, -8));
				EMIT(&p, BPF_ATOMIC_OP(size, BPF_STORE_REL, rb, rd, 0));
				EMIT(&p, BPF_LDX_MEM(BPF_DW, rd, BPF_REG_10, -8));
				prog_end(&p, rd);
				snprintf(desc, sizeof(desc), "store_rel size=%d base=r%d src=r%d",
					 size, rb, rd);
				fd = prog_load(&p, desc);
				if (fd < 0)
					return;
				for (j = 0; j < ARRAY_SIZE(inputs); j++) {
					__u64 in1 = inputs[(j + 1) % ARRAY_SIZE(inputs)];

					prog_run(fd, inputs[j], in1,
						 head_merge(inputs[j], in1, size), desc);
				}
				close(fd);
			}
		}
	}
}

/*
 * A jump with the maximal offset over instructions which expand to several
 * native ones, so that the native offset outgrows the field of a short
 * branch and takes the JITs through their branch conversion. The region
 * must not grow in the verifier, or the jump could not reach over it any
 * more: the divisions are by constants, as a division by a register gets
 * a zero check in front, and the region sticks to one width, as a 32-bit
 * result read by a 64-bit instruction gets a zero extension on the JITs
 * which need one. The region is the not taken path and ends with a result
 * different from the taken one, so a jump landing inside it fails the run.
 */
static void max_jmp(bool alu32, bool taken)
{
	static const int ops[] = {
		BPF_LSH, BPF_RSH, BPF_ARSH, BPF_ADD, BPF_SUB, BPF_MUL, BPF_DIV, BPF_MOD,
	};
	struct { __u64 r0, r2; } ctx = { taken, 0x0123456789abcdefULL };
	LIBBPF_OPTS(bpf_test_run_opts, opts,
		.ctx_in = &ctx,
		.ctx_size_in = sizeof(ctx),
	);
	int len = INT16_MAX + 5, i = 0, fd;
	struct bpf_insn *insns;
	char desc[64];

	snprintf(desc, sizeof(desc), "max jump alu%d %s", alu32 ? 32 : 64,
		 taken ? "taken" : "not taken");
	insns = malloc(len * sizeof(*insns));
	if (!ASSERT_OK_PTR(insns, "insns"))
		return;
	insns[i++] = BPF_LDX_MEM(BPF_DW, BPF_REG_2, BPF_REG_1, 8);
	insns[i++] = BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, 0);
	insns[i++] = BPF_MOV64_REG(BPF_REG_3, BPF_REG_2);
	insns[i++] = BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 1, INT16_MAX);
	while (i < len - 2) {
		int op = ops[(i >> 1) % ARRAY_SIZE(ops)];

		if (op == BPF_DIV || op == BPF_MOD)
			insns[i++] = alu32 ? BPF_ALU32_IMM(op, BPF_REG_3, 7) :
					     BPF_ALU64_IMM(op, BPF_REG_3, 7);
		else
			insns[i++] = alu32 ? BPF_ALU32_REG(op, BPF_REG_3, BPF_REG_2) :
					     BPF_ALU64_REG(op, BPF_REG_3, BPF_REG_2);
	}
	insns[i++] = BPF_ALU64_IMM(BPF_MOV, BPF_REG_0, 2);
	insns[i++] = BPF_EXIT_INSN();
	ASSERT_EQ(i, len, "insn count");

	fd = load_prog(insns, i, desc);
	if (fd >= 0) {
		if (ASSERT_OK(bpf_prog_test_run_opts(fd, &opts), desc))
			ASSERT_EQ(opts.retval, taken ? 1 : 2, desc);
		close(fd);
	}
	free(insns);
}

static void test_max_jmp(void)
{
	max_jmp(false, true);
	max_jmp(false, false);
	max_jmp(true, true);
	max_jmp(true, false);
}

void test_jit_insns(void)
{
	RUN_TESTS(jit_insns_arena);
	if (test__start_subtest("alu_imm"))
		test_alu_imm();
	if (test__start_subtest("alu_reg"))
		test_alu_reg();
	if (test__start_subtest("alu_imm_regs"))
		test_alu_imm_regs();
	if (test__start_subtest("div_const"))
		test_div_const();
	if (test__start_subtest("alu_reg_pairs"))
		test_alu_reg_pairs();
	if (test__start_subtest("shifts"))
		test_shifts();
	if (test__start_subtest("atomics"))
		test_atomics();
	if (test__start_subtest("atomic_reg_pairs"))
		test_atomic_reg_pairs();
	if (test__start_subtest("xadd_chain"))
		test_xadd_chain();
	if (test__start_subtest("ld_imm64"))
		test_ld_imm64();
	if (test__start_subtest("jmp_imm"))
		test_jmp_imm();
	if (test__start_subtest("jmp_reg"))
		test_jmp_reg();
	if (test__start_subtest("jmp_reg_pairs"))
		test_jmp_reg_pairs();
	if (test__start_subtest("staggered_jumps"))
		test_staggered_jumps();
	if (test__start_subtest("max_jmp"))
		test_max_jmp();
	if (test__start_subtest("ldst_zero_off"))
		test_ldst_zero_off();
	if (test__start_subtest("ldst_offsets"))
		test_ldst_offsets();
	if (test__start_subtest("acq_rel"))
		test_acq_rel();
	if (test__start_subtest("movsx"))
		test_movsx();
	if (test__start_subtest("jset_imm"))
		test_jset_imm();
	if (test__start_subtest("cmp_imm"))
		test_cmp_imm();
	if (test__start_subtest("alu_consts"))
		test_alu_consts();
	if (test__start_subtest("bswap"))
		test_bswap();
	if (test__start_subtest("probe_mem"))
		test_probe_mem();
}

/* The families with an immediate once more with the constants blinded */
void serial_test_jit_insns_blinded(void)
{
	const char *harden = "/proc/sys/net/core/bpf_jit_harden";
	char old[16] = {};

	if (!is_jit_enabled()) {
		test__skip();
		return;
	}
	if (sysctl_set_or_fail(harden, old, "2"))
		return;
	if (test__start_subtest("alu_imm"))
		test_alu_imm();
	if (test__start_subtest("alu_imm_regs"))
		test_alu_imm_regs();
	if (test__start_subtest("div_const"))
		test_div_const();
	if (test__start_subtest("shifts"))
		test_shifts();
	if (test__start_subtest("xadd_chain"))
		test_xadd_chain();
	if (test__start_subtest("ld_imm64"))
		test_ld_imm64();
	if (test__start_subtest("jmp_imm"))
		test_jmp_imm();
	if (test__start_subtest("ldst_zero_off"))
		test_ldst_zero_off();
	if (test__start_subtest("ldst_offsets"))
		test_ldst_offsets();
	if (test__start_subtest("jset_imm"))
		test_jset_imm();
	if (test__start_subtest("cmp_imm"))
		test_cmp_imm();
	if (test__start_subtest("alu_consts"))
		test_alu_consts();
	sysctl_set_or_fail(harden, NULL, old);
}
