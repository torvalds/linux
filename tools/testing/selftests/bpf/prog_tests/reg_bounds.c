// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#define _GNU_SOURCE
#include <limits.h>
#include <test_progs.h>
#include <linux/filter.h>
#include <linux/bpf.h>

/* =================================
 * SHORT AND CONSISTENT NUMBER TYPES
 * =================================
 */
#define U64_MAX ((u64)UINT64_MAX)
#define U32_MAX ((u32)UINT_MAX)
#define U16_MAX ((u32)UINT_MAX)
#define S64_MIN ((s64)INT64_MIN)
#define S64_MAX ((s64)INT64_MAX)
#define S32_MIN ((s32)INT_MIN)
#define S32_MAX ((s32)INT_MAX)
#define S16_MIN ((s16)0x80000000)
#define S16_MAX ((s16)0x7fffffff)

typedef unsigned long long ___u64;
typedef unsigned int ___u32;
typedef long long ___s64;
typedef int ___s32;

/* avoid conflicts with already defined types in kernel headers */
#define u64 ___u64
#define u32 ___u32
#define s64 ___s64
#define s32 ___s32

/* ==================================
 * STRING BUF ABSTRACTION AND HELPERS
 * ==================================
 */
struct strbuf {
	size_t buf_sz;
	int pos;
	char buf[0];
};

#define DEFINE_STRBUF(name, N)						\
	struct { struct strbuf buf; char data[(N)]; } ___##name;	\
	struct strbuf *name = (___##name.buf.buf_sz = (N), ___##name.buf.pos = 0, &___##name.buf)

__printf(2, 3)
static inline void snappendf(struct strbuf *s, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	s->pos += vsnprintf(s->buf + s->pos,
			    s->pos < s->buf_sz ? s->buf_sz - s->pos : 0,
			    fmt, args);
	va_end(args);
}

/* ==================================
 * GENERIC NUMBER TYPE AND OPERATIONS
 * ==================================
 */
enum num_t { U64, first_t = U64, U32, S64, S32, last_t = S32 };

static __always_inline u64 min_t(enum num_t t, u64 x, u64 y)
{
	switch (t) {
	case U64: return (u64)x < (u64)y ? (u64)x : (u64)y;
	case U32: return (u32)x < (u32)y ? (u32)x : (u32)y;
	case S64: return (s64)x < (s64)y ? (s64)x : (s64)y;
	case S32: return (s32)x < (s32)y ? (s32)x : (s32)y;
	default: printf("min_t!\n"); exit(1);
	}
}

static __always_inline u64 max_t(enum num_t t, u64 x, u64 y)
{
	switch (t) {
	case U64: return (u64)x > (u64)y ? (u64)x : (u64)y;
	case U32: return (u32)x > (u32)y ? (u32)x : (u32)y;
	case S64: return (s64)x > (s64)y ? (s64)x : (s64)y;
	case S32: return (s32)x > (s32)y ? (u32)(s32)x : (u32)(s32)y;
	default: printf("max_t!\n"); exit(1);
	}
}

static __always_inline u64 cast_t(enum num_t t, u64 x)
{
	switch (t) {
	case U64: return (u64)x;
	case U32: return (u32)x;
	case S64: return (s64)x;
	case S32: return (u32)(s32)x;
	default: printf("cast_t!\n"); exit(1);
	}
}

static const char *t_str(enum num_t t)
{
	switch (t) {
	case U64: return "u64";
	case U32: return "u32";
	case S64: return "s64";
	case S32: return "s32";
	default: printf("t_str!\n"); exit(1);
	}
}

static enum num_t t_is_32(enum num_t t)
{
	switch (t) {
	case U64: return false;
	case U32: return true;
	case S64: return false;
	case S32: return true;
	default: printf("t_is_32!\n"); exit(1);
	}
}

static enum num_t t_signed(enum num_t t)
{
	switch (t) {
	case U64: return S64;
	case U32: return S32;
	case S64: return S64;
	case S32: return S32;
	default: printf("t_signed!\n"); exit(1);
	}
}

static enum num_t t_unsigned(enum num_t t)
{
	switch (t) {
	case U64: return U64;
	case U32: return U32;
	case S64: return U64;
	case S32: return U32;
	default: printf("t_unsigned!\n"); exit(1);
	}
}

#define UNUM_MAX_DECIMAL U16_MAX
#define SNUM_MAX_DECIMAL S16_MAX
#define SNUM_MIN_DECIMAL S16_MIN

static bool num_is_small(enum num_t t, u64 x)
{
	switch (t) {
	case U64: return (u64)x <= UNUM_MAX_DECIMAL;
	case U32: return (u32)x <= UNUM_MAX_DECIMAL;
	case S64: return (s64)x >= SNUM_MIN_DECIMAL && (s64)x <= SNUM_MAX_DECIMAL;
	case S32: return (s32)x >= SNUM_MIN_DECIMAL && (s32)x <= SNUM_MAX_DECIMAL;
	default: printf("num_is_small!\n"); exit(1);
	}
}

static void snprintf_num(enum num_t t, struct strbuf *sb, u64 x)
{
	bool is_small = num_is_small(t, x);

	if (is_small) {
		switch (t) {
		case U64: return snappendf(sb, "%llu", (u64)x);
		case U32: return snappendf(sb, "%u", (u32)x);
		case S64: return snappendf(sb, "%lld", (s64)x);
		case S32: return snappendf(sb, "%d", (s32)x);
		default: printf("snprintf_num!\n"); exit(1);
		}
	} else {
		switch (t) {
		case U64:
			if (x == U64_MAX)
				return snappendf(sb, "U64_MAX");
			else if (x >= U64_MAX - 256)
				return snappendf(sb, "U64_MAX-%llu", U64_MAX - x);
			else
				return snappendf(sb, "%#llx", (u64)x);
		case U32:
			if ((u32)x == U32_MAX)
				return snappendf(sb, "U32_MAX");
			else if ((u32)x >= U32_MAX - 256)
				return snappendf(sb, "U32_MAX-%u", U32_MAX - (u32)x);
			else
				return snappendf(sb, "%#x", (u32)x);
		case S64:
			if ((s64)x == S64_MAX)
				return snappendf(sb, "S64_MAX");
			else if ((s64)x >= S64_MAX - 256)
				return snappendf(sb, "S64_MAX-%lld", S64_MAX - (s64)x);
			else if ((s64)x == S64_MIN)
				return snappendf(sb, "S64_MIN");
			else if ((s64)x <= S64_MIN + 256)
				return snappendf(sb, "S64_MIN+%lld", (s64)x - S64_MIN);
			else
				return snappendf(sb, "%#llx", (s64)x);
		case S32:
			if ((s32)x == S32_MAX)
				return snappendf(sb, "S32_MAX");
			else if ((s32)x >= S32_MAX - 256)
				return snappendf(sb, "S32_MAX-%d", S32_MAX - (s32)x);
			else if ((s32)x == S32_MIN)
				return snappendf(sb, "S32_MIN");
			else if ((s32)x <= S32_MIN + 256)
				return snappendf(sb, "S32_MIN+%d", (s32)x - S32_MIN);
			else
				return snappendf(sb, "%#x", (s32)x);
		default: printf("snprintf_num!\n"); exit(1);
		}
	}
}

/* ===================================
 * GENERIC RANGE STRUCT AND OPERATIONS
 * ===================================
 */
struct range {
	u64 a, b;
};

static void snprintf_range(enum num_t t, struct strbuf *sb, struct range x)
{
	if (x.a == x.b)
		return snprintf_num(t, sb, x.a);

	snappendf(sb, "[");
	snprintf_num(t, sb, x.a);
	snappendf(sb, "; ");
	snprintf_num(t, sb, x.b);
	snappendf(sb, "]");
}

static void print_range(enum num_t t, struct range x, const char *sfx)
{
	DEFINE_STRBUF(sb, 128);

	snprintf_range(t, sb, x);
	printf("%s%s", sb->buf, sfx);
}

static const struct range unkn[] = {
	[U64] = { 0, U64_MAX },
	[U32] = { 0, U32_MAX },
	[S64] = { (u64)S64_MIN, (u64)S64_MAX },
	[S32] = { (u64)(u32)S32_MIN, (u64)(u32)S32_MAX },
};

static struct range unkn_subreg(enum num_t t)
{
	switch (t) {
	case U64: return unkn[U32];
	case U32: return unkn[U32];
	case S64: return unkn[U32];
	case S32: return unkn[S32];
	default: printf("unkn_subreg!\n"); exit(1);
	}
}

static struct range range(enum num_t t, u64 a, u64 b)
{
	switch (t) {
	case U64: return (struct range){ (u64)a, (u64)b };
	case U32: return (struct range){ (u32)a, (u32)b };
	case S64: return (struct range){ (s64)a, (s64)b };
	case S32: return (struct range){ (u32)(s32)a, (u32)(s32)b };
	default: printf("range!\n"); exit(1);
	}
}

static __always_inline u32 sign64(u64 x) { return (x >> 63) & 1; }
static __always_inline u32 sign32(u64 x) { return ((u32)x >> 31) & 1; }
static __always_inline u32 upper32(u64 x) { return (u32)(x >> 32); }
static __always_inline u64 swap_low32(u64 x, u32 y) { return (x & 0xffffffff00000000ULL) | y; }

static bool range_eq(struct range x, struct range y)
{
	return x.a == y.a && x.b == y.b;
}

static struct range range_cast_to_s32(struct range x)
{
	u64 a = x.a, b = x.b;

	/* if upper 32 bits are constant, lower 32 bits should form a proper
	 * s32 range to be correct
	 */
	if (upper32(a) == upper32(b) && (s32)a <= (s32)b)
		return range(S32, a, b);

	/* Special case where upper bits form a small sequence of two
	 * sequential numbers (in 32-bit unsigned space, so 0xffffffff to
	 * 0x00000000 is also valid), while lower bits form a proper s32 range
	 * going from negative numbers to positive numbers.
	 *
	 * E.g.: [0xfffffff0ffffff00; 0xfffffff100000010]. Iterating
	 * over full 64-bit numbers range will form a proper [-16, 16]
	 * ([0xffffff00; 0x00000010]) range in its lower 32 bits.
	 */
	if (upper32(a) + 1 == upper32(b) && (s32)a < 0 && (s32)b >= 0)
		return range(S32, a, b);

	/* otherwise we can't derive much meaningful information */
	return unkn[S32];
}

static struct range range_cast_u64(enum num_t to_t, struct range x)
{
	u64 a = (u64)x.a, b = (u64)x.b;

	switch (to_t) {
	case U64:
		return x;
	case U32:
		if (upper32(a) != upper32(b))
			return unkn[U32];
		return range(U32, a, b);
	case S64:
		if (sign64(a) != sign64(b))
			return unkn[S64];
		return range(S64, a, b);
	case S32:
		return range_cast_to_s32(x);
	default: printf("range_cast_u64!\n"); exit(1);
	}
}

static struct range range_cast_s64(enum num_t to_t, struct range x)
{
	s64 a = (s64)x.a, b = (s64)x.b;

	switch (to_t) {
	case U64:
		/* equivalent to (s64)a <= (s64)b check */
		if (sign64(a) != sign64(b))
			return unkn[U64];
		return range(U64, a, b);
	case U32:
		if (upper32(a) != upper32(b) || sign32(a) != sign32(b))
			return unkn[U32];
		return range(U32, a, b);
	case S64:
		return x;
	case S32:
		return range_cast_to_s32(x);
	default: printf("range_cast_s64!\n"); exit(1);
	}
}

static struct range range_cast_u32(enum num_t to_t, struct range x)
{
	u32 a = (u32)x.a, b = (u32)x.b;

	switch (to_t) {
	case U64:
	case S64:
		/* u32 is always a valid zero-extended u64/s64 */
		return range(to_t, a, b);
	case U32:
		return x;
	case S32:
		return range_cast_to_s32(range(U32, a, b));
	default: printf("range_cast_u32!\n"); exit(1);
	}
}

static struct range range_cast_s32(enum num_t to_t, struct range x)
{
	s32 a = (s32)x.a, b = (s32)x.b;

	switch (to_t) {
	case U64:
	case U32:
	case S64:
		if (sign32(a) != sign32(b))
			return unkn[to_t];
		return range(to_t, a, b);
	case S32:
		return x;
	default: printf("range_cast_s32!\n"); exit(1);
	}
}

/* Reinterpret range in *from_t* domain as a range in *to_t* domain preserving
 * all possible information. Worst case, it will be unknown range within
 * *to_t* domain, if nothing more specific can be guaranteed during the
 * conversion
 */
static struct range range_cast(enum num_t from_t, enum num_t to_t, struct range from)
{
	switch (from_t) {
	case U64: return range_cast_u64(to_t, from);
	case U32: return range_cast_u32(to_t, from);
	case S64: return range_cast_s64(to_t, from);
	case S32: return range_cast_s32(to_t, from);
	default: printf("range_cast!\n"); exit(1);
	}
}

static bool is_valid_num(enum num_t t, u64 x)
{
	switch (t) {
	case U64: return true;
	case U32: return upper32(x) == 0;
	case S64: return true;
	case S32: return upper32(x) == 0;
	default: printf("is_valid_num!\n"); exit(1);
	}
}

static bool is_valid_range(enum num_t t, struct range x)
{
	if (!is_valid_num(t, x.a) || !is_valid_num(t, x.b))
		return false;

	switch (t) {
	case U64: return (u64)x.a <= (u64)x.b;
	case U32: return (u32)x.a <= (u32)x.b;
	case S64: return (s64)x.a <= (s64)x.b;
	case S32: return (s32)x.a <= (s32)x.b;
	default: printf("is_valid_range!\n"); exit(1);
	}
}

static struct range range_improve(enum num_t t, struct range old, struct range new)
{
	return range(t, max_t(t, old.a, new.a), min_t(t, old.b, new.b));
}

static struct range range_refine(enum num_t x_t, struct range x, enum num_t y_t, struct range y)
{
	struct range y_cast;

	y_cast = range_cast(y_t, x_t, y);

	/* If we know that
	 *   - *x* is in the range of signed 32bit value, and
	 *   - *y_cast* range is 32-bit signed non-negative
	 * then *x* range can be improved with *y_cast* such that *x* range
	 * is 32-bit signed non-negative. Otherwise, if the new range for *x*
	 * allows upper 32-bit * 0xffffffff then the eventual new range for
	 * *x* will be out of signed 32-bit range which violates the origin
	 * *x* range.
	 */
	if (x_t == S64 && y_t == S32 && y_cast.a <= S32_MAX  && y_cast.b <= S32_MAX &&
	    (s64)x.a >= S32_MIN && (s64)x.b <= S32_MAX)
		return range_improve(x_t, x, y_cast);

	/* the case when new range knowledge, *y*, is a 32-bit subregister
	 * range, while previous range knowledge, *x*, is a full register
	 * 64-bit range, needs special treatment to take into account upper 32
	 * bits of full register range
	 */
	if (t_is_32(y_t) && !t_is_32(x_t)) {
		struct range x_swap;

		/* some combinations of upper 32 bits and sign bit can lead to
		 * invalid ranges, in such cases it's easier to detect them
		 * after cast/swap than try to enumerate all the conditions
		 * under which transformation and knowledge transfer is valid
		 */
		x_swap = range(x_t, swap_low32(x.a, y_cast.a), swap_low32(x.b, y_cast.b));
		if (!is_valid_range(x_t, x_swap))
			return x;
		return range_improve(x_t, x, x_swap);
	}

	if (!t_is_32(x_t) && !t_is_32(y_t) && x_t != y_t) {
		if (x_t == S64 && x.a > x.b) {
			if (x.b < y.a && x.a <= y.b)
				return range(x_t, x.a, y.b);
			if (x.a > y.b && x.b >= y.a)
				return range(x_t, y.a, x.b);
		} else if (x_t == U64 && y.a > y.b) {
			if (y.b < x.a && y.a <= x.b)
				return range(x_t, y.a, x.b);
			if (y.a > x.b && y.b >= x.a)
				return range(x_t, x.a, y.b);
		}
	}

	/* otherwise, plain range cast and intersection works */
	return range_improve(x_t, x, y_cast);
}

/* =======================
 * GENERIC CONDITIONAL OPS
 * =======================
 */
enum op { OP_LT, OP_LE, OP_GT, OP_GE, OP_EQ, OP_NE, first_op = OP_LT, last_op = OP_NE };

static enum op complement_op(enum op op)
{
	switch (op) {
	case OP_LT: return OP_GE;
	case OP_LE: return OP_GT;
	case OP_GT: return OP_LE;
	case OP_GE: return OP_LT;
	case OP_EQ: return OP_NE;
	case OP_NE: return OP_EQ;
	default: printf("complement_op!\n"); exit(1);
	}
}

static const char *op_str(enum op op)
{
	switch (op) {
	case OP_LT: return "<";
	case OP_LE: return "<=";
	case OP_GT: return ">";
	case OP_GE: return ">=";
	case OP_EQ: return "==";
	case OP_NE: return "!=";
	default: printf("op_str!\n"); exit(1);
	}
}

/* Can register with range [x.a, x.b] *EVER* satisfy
 * OP (<, <=, >, >=, ==, !=) relation to
 * a register with range [y.a, y.b]
 * _in *num_t* domain_
 */
static bool range_canbe_op(enum num_t t, struct range x, struct range y, enum op op)
{
#define range_canbe(T) do {									\
	switch (op) {										\
	case OP_LT: return (T)x.a < (T)y.b;							\
	case OP_LE: return (T)x.a <= (T)y.b;							\
	case OP_GT: return (T)x.b > (T)y.a;							\
	case OP_GE: return (T)x.b >= (T)y.a;							\
	case OP_EQ: return (T)max_t(t, x.a, y.a) <= (T)min_t(t, x.b, y.b);			\
	case OP_NE: return !((T)x.a == (T)x.b && (T)y.a == (T)y.b && (T)x.a == (T)y.a);		\
	default: printf("range_canbe op %d\n", op); exit(1);					\
	}											\
} while (0)

	switch (t) {
	case U64: { range_canbe(u64); }
	case U32: { range_canbe(u32); }
	case S64: { range_canbe(s64); }
	case S32: { range_canbe(s32); }
	default: printf("range_canbe!\n"); exit(1);
	}
#undef range_canbe
}

/* Does register with range [x.a, x.b] *ALWAYS* satisfy
 * OP (<, <=, >, >=, ==, !=) relation to
 * a register with range [y.a, y.b]
 * _in *num_t* domain_
 */
static bool range_always_op(enum num_t t, struct range x, struct range y, enum op op)
{
	/* always op <=> ! canbe complement(op) */
	return !range_canbe_op(t, x, y, complement_op(op));
}

/* Does register with range [x.a, x.b] *NEVER* satisfy
 * OP (<, <=, >, >=, ==, !=) relation to
 * a register with range [y.a, y.b]
 * _in *num_t* domain_
 */
static bool range_never_op(enum num_t t, struct range x, struct range y, enum op op)
{
	return !range_canbe_op(t, x, y, op);
}

/* similar to verifier's is_branch_taken():
 *    1 - always taken;
 *    0 - never taken,
 *   -1 - unsure.
 */
static int range_branch_taken_op(enum num_t t, struct range x, struct range y, enum op op)
{
	if (range_always_op(t, x, y, op))
		return 1;
	if (range_never_op(t, x, y, op))
		return 0;
	return -1;
}

/* What would be the new estimates for register x and y ranges assuming truthful
 * OP comparison between them. I.e., (x OP y == true) => x <- newx, y <- newy.
 *
 * We assume "interesting" cases where ranges overlap. Cases where it's
 * obvious that (x OP y) is either always true or false should be filtered with
 * range_never and range_always checks.
 */
static void range_cond(enum num_t t, struct range x, struct range y,
		       enum op op, struct range *newx, struct range *newy)
{
	if (!range_canbe_op(t, x, y, op)) {
		/* nothing to adjust, can't happen, return original values */
		*newx = x;
		*newy = y;
		return;
	}
	switch (op) {
	case OP_LT:
		*newx = range(t, x.a, min_t(t, x.b, y.b - 1));
		*newy = range(t, max_t(t, x.a + 1, y.a), y.b);
		break;
	case OP_LE:
		*newx = range(t, x.a, min_t(t, x.b, y.b));
		*newy = range(t, max_t(t, x.a, y.a), y.b);
		break;
	case OP_GT:
		*newx = range(t, max_t(t, x.a, y.a + 1), x.b);
		*newy = range(t, y.a, min_t(t, x.b - 1, y.b));
		break;
	case OP_GE:
		*newx = range(t, max_t(t, x.a, y.a), x.b);
		*newy = range(t, y.a, min_t(t, x.b, y.b));
		break;
	case OP_EQ:
		*newx = range(t, max_t(t, x.a, y.a), min_t(t, x.b, y.b));
		*newy = range(t, max_t(t, x.a, y.a), min_t(t, x.b, y.b));
		break;
	case OP_NE:
		/* below logic is supported by the verifier now */
		if (x.a == x.b && x.a == y.a) {
			/* X is a constant matching left side of Y */
			*newx = range(t, x.a, x.b);
			*newy = range(t, y.a + 1, y.b);
		} else if (x.a == x.b && x.b == y.b) {
			/* X is a constant matching rigth side of Y */
			*newx = range(t, x.a, x.b);
			*newy = range(t, y.a, y.b - 1);
		} else if (y.a == y.b && x.a == y.a) {
			/* Y is a constant matching left side of X */
			*newx = range(t, x.a + 1, x.b);
			*newy = range(t, y.a, y.b);
		} else if (y.a == y.b && x.b == y.b) {
			/* Y is a constant matching rigth side of X */
			*newx = range(t, x.a, x.b - 1);
			*newy = range(t, y.a, y.b);
		} else {
			/* generic case, can't derive more information */
			*newx = range(t, x.a, x.b);
			*newy = range(t, y.a, y.b);
		}

		break;
	default:
		break;
	}
}

/* =======================
 * REGISTER STATE HANDLING
 * =======================
 */
struct reg_state {
	struct range r[4]; /* indexed by enum num_t: U64, U32, S64, S32 */
	bool valid;
};

static void print_reg_state(struct reg_state *r, const char *sfx)
{
	DEFINE_STRBUF(sb, 512);
	enum num_t t;
	int cnt = 0;

	if (!r->valid) {
		printf("<not found>%s", sfx);
		return;
	}

	snappendf(sb, "scalar(");
	for (t = first_t; t <= last_t; t++) {
		snappendf(sb, "%s%s=", cnt++ ? "," : "", t_str(t));
		snprintf_range(t, sb, r->r[t]);
	}
	snappendf(sb, ")");

	printf("%s%s", sb->buf, sfx);
}

static void print_refinement(enum num_t s_t, struct range src,
			     enum num_t d_t, struct range old, struct range new,
			     const char *ctx)
{
	printf("REFINING (%s) (%s)SRC=", ctx, t_str(s_t));
	print_range(s_t, src, "");
	printf(" (%s)DST_OLD=", t_str(d_t));
	print_range(d_t, old, "");
	printf(" (%s)DST_NEW=", t_str(d_t));
	print_range(d_t, new, "\n");
}

static void reg_state_refine(struct reg_state *r, enum num_t t, struct range x, const char *ctx)
{
	enum num_t d_t, s_t;
	struct range old;
	bool keep_going = false;

again:
	/* try to derive new knowledge from just learned range x of type t */
	for (d_t = first_t; d_t <= last_t; d_t++) {
		old = r->r[d_t];
		r->r[d_t] = range_refine(d_t, r->r[d_t], t, x);
		if (!range_eq(r->r[d_t], old)) {
			keep_going = true;
			if (env.verbosity >= VERBOSE_VERY)
				print_refinement(t, x, d_t, old, r->r[d_t], ctx);
		}
	}

	/* now see if we can derive anything new from updated reg_state's ranges */
	for (s_t = first_t; s_t <= last_t; s_t++) {
		for (d_t = first_t; d_t <= last_t; d_t++) {
			old = r->r[d_t];
			r->r[d_t] = range_refine(d_t, r->r[d_t], s_t, r->r[s_t]);
			if (!range_eq(r->r[d_t], old)) {
				keep_going = true;
				if (env.verbosity >= VERBOSE_VERY)
					print_refinement(s_t, r->r[s_t], d_t, old, r->r[d_t], ctx);
			}
		}
	}

	/* keep refining until we converge */
	if (keep_going) {
		keep_going = false;
		goto again;
	}
}

static void reg_state_set_const(struct reg_state *rs, enum num_t t, u64 val)
{
	enum num_t tt;

	rs->valid = true;
	for (tt = first_t; tt <= last_t; tt++)
		rs->r[tt] = tt == t ? range(t, val, val) : unkn[tt];

	reg_state_refine(rs, t, rs->r[t], "CONST");
}

static void reg_state_cond(enum num_t t, struct reg_state *x, struct reg_state *y, enum op op,
			   struct reg_state *newx, struct reg_state *newy, const char *ctx)
{
	char buf[32];
	enum num_t ts[2];
	struct reg_state xx = *x, yy = *y;
	int i, t_cnt;
	struct range z1, z2;

	if (op == OP_EQ || op == OP_NE) {
		/* OP_EQ and OP_NE are sign-agnostic, so we need to process
		 * both signed and unsigned domains at the same time
		 */
		ts[0] = t_unsigned(t);
		ts[1] = t_signed(t);
		t_cnt = 2;
	} else {
		ts[0] = t;
		t_cnt = 1;
	}

	for (i = 0; i < t_cnt; i++) {
		t = ts[i];
		z1 = x->r[t];
		z2 = y->r[t];

		range_cond(t, z1, z2, op, &z1, &z2);

		if (newx) {
			snprintf(buf, sizeof(buf), "%s R1", ctx);
			reg_state_refine(&xx, t, z1, buf);
		}
		if (newy) {
			snprintf(buf, sizeof(buf), "%s R2", ctx);
			reg_state_refine(&yy, t, z2, buf);
		}
	}

	if (newx)
		*newx = xx;
	if (newy)
		*newy = yy;
}

static int reg_state_branch_taken_op(enum num_t t, struct reg_state *x, struct reg_state *y,
				     enum op op)
{
	if (op == OP_EQ || op == OP_NE) {
		/* OP_EQ and OP_NE are sign-agnostic */
		enum num_t tu = t_unsigned(t);
		enum num_t ts = t_signed(t);
		int br_u, br_s, br;

		br_u = range_branch_taken_op(tu, x->r[tu], y->r[tu], op);
		br_s = range_branch_taken_op(ts, x->r[ts], y->r[ts], op);

		if (br_u >= 0 && br_s >= 0 && br_u != br_s)
			ASSERT_FALSE(true, "branch taken inconsistency!\n");

		/* if 64-bit ranges are indecisive, use 32-bit subranges to
		 * eliminate always/never taken branches, if possible
		 */
		if (br_u == -1 && (t == U64 || t == S64)) {
			br = range_branch_taken_op(U32, x->r[U32], y->r[U32], op);
			/* we can only reject for OP_EQ, never take branch
			 * based on lower 32 bits
			 */
			if (op == OP_EQ && br == 0)
				return 0;
			/* for OP_NEQ we can be conclusive only if lower 32 bits
			 * differ and thus inequality branch is always taken
			 */
			if (op == OP_NE && br == 1)
				return 1;

			br = range_branch_taken_op(S32, x->r[S32], y->r[S32], op);
			if (op == OP_EQ && br == 0)
				return 0;
			if (op == OP_NE && br == 1)
				return 1;
		}

		return br_u >= 0 ? br_u : br_s;
	}
	return range_branch_taken_op(t, x->r[t], y->r[t], op);
}

/* =====================================
 * BPF PROGS GENERATION AND VERIFICATION
 * =====================================
 */
struct case_spec {
	/* whether to init full register (r1) or sub-register (w1) */
	bool init_subregs;
	/* whether to establish initial value range on full register (r1) or
	 * sub-register (w1)
	 */
	bool setup_subregs;
	/* whether to establish initial value range using signed or unsigned
	 * comparisons (i.e., initialize umin/umax or smin/smax directly)
	 */
	bool setup_signed;
	/* whether to perform comparison on full registers or sub-registers */
	bool compare_subregs;
	/* whether to perform comparison using signed or unsigned operations */
	bool compare_signed;
};

/* Generate test BPF program based on provided test ranges, operation, and
 * specifications about register bitness and signedness.
 */
static int load_range_cmp_prog(struct range x, struct range y, enum op op,
			       int branch_taken, struct case_spec spec,
			       char *log_buf, size_t log_sz,
			       int *false_pos, int *true_pos)
{
#define emit(insn) ({							\
	struct bpf_insn __insns[] = { insn };				\
	int __i;							\
	for (__i = 0; __i < ARRAY_SIZE(__insns); __i++)			\
		insns[cur_pos + __i] = __insns[__i];			\
	cur_pos += __i;							\
})
#define JMP_TO(target) (target - cur_pos - 1)
	int cur_pos = 0, exit_pos, fd, op_code;
	struct bpf_insn insns[64];
	LIBBPF_OPTS(bpf_prog_load_opts, opts,
		.log_level = 2,
		.log_buf = log_buf,
		.log_size = log_sz,
		.prog_flags = testing_prog_flags(),
	);

	/* ; skip exit block below
	 * goto +2;
	 */
	emit(BPF_JMP_A(2));
	exit_pos = cur_pos;
	/* ; exit block for all the preparatory conditionals
	 * out:
	 * r0 = 0;
	 * exit;
	 */
	emit(BPF_MOV64_IMM(BPF_REG_0, 0));
	emit(BPF_EXIT_INSN());
	/*
	 * ; assign r6/w6 and r7/w7 unpredictable u64/u32 value
	 * call bpf_get_current_pid_tgid;
	 * r6 = r0;               | w6 = w0;
	 * call bpf_get_current_pid_tgid;
	 * r7 = r0;               | w7 = w0;
	 */
	emit(BPF_EMIT_CALL(BPF_FUNC_get_current_pid_tgid));
	if (spec.init_subregs)
		emit(BPF_MOV32_REG(BPF_REG_6, BPF_REG_0));
	else
		emit(BPF_MOV64_REG(BPF_REG_6, BPF_REG_0));
	emit(BPF_EMIT_CALL(BPF_FUNC_get_current_pid_tgid));
	if (spec.init_subregs)
		emit(BPF_MOV32_REG(BPF_REG_7, BPF_REG_0));
	else
		emit(BPF_MOV64_REG(BPF_REG_7, BPF_REG_0));
	/* ; setup initial r6/w6 possible value range ([x.a, x.b])
	 * r1 = %[x.a] ll;        | w1 = %[x.a];
	 * r2 = %[x.b] ll;        | w2 = %[x.b];
	 * if r6 < r1 goto out;   | if w6 < w1 goto out;
	 * if r6 > r2 goto out;   | if w6 > w2 goto out;
	 */
	if (spec.setup_subregs) {
		emit(BPF_MOV32_IMM(BPF_REG_1, (s32)x.a));
		emit(BPF_MOV32_IMM(BPF_REG_2, (s32)x.b));
		emit(BPF_JMP32_REG(spec.setup_signed ? BPF_JSLT : BPF_JLT,
				   BPF_REG_6, BPF_REG_1, JMP_TO(exit_pos)));
		emit(BPF_JMP32_REG(spec.setup_signed ? BPF_JSGT : BPF_JGT,
				   BPF_REG_6, BPF_REG_2, JMP_TO(exit_pos)));
	} else {
		emit(BPF_LD_IMM64(BPF_REG_1, x.a));
		emit(BPF_LD_IMM64(BPF_REG_2, x.b));
		emit(BPF_JMP_REG(spec.setup_signed ? BPF_JSLT : BPF_JLT,
				 BPF_REG_6, BPF_REG_1, JMP_TO(exit_pos)));
		emit(BPF_JMP_REG(spec.setup_signed ? BPF_JSGT : BPF_JGT,
				 BPF_REG_6, BPF_REG_2, JMP_TO(exit_pos)));
	}
	/* ; setup initial r7/w7 possible value range ([y.a, y.b])
	 * r1 = %[y.a] ll;        | w1 = %[y.a];
	 * r2 = %[y.b] ll;        | w2 = %[y.b];
	 * if r7 < r1 goto out;   | if w7 < w1 goto out;
	 * if r7 > r2 goto out;   | if w7 > w2 goto out;
	 */
	if (spec.setup_subregs) {
		emit(BPF_MOV32_IMM(BPF_REG_1, (s32)y.a));
		emit(BPF_MOV32_IMM(BPF_REG_2, (s32)y.b));
		emit(BPF_JMP32_REG(spec.setup_signed ? BPF_JSLT : BPF_JLT,
				   BPF_REG_7, BPF_REG_1, JMP_TO(exit_pos)));
		emit(BPF_JMP32_REG(spec.setup_signed ? BPF_JSGT : BPF_JGT,
				   BPF_REG_7, BPF_REG_2, JMP_TO(exit_pos)));
	} else {
		emit(BPF_LD_IMM64(BPF_REG_1, y.a));
		emit(BPF_LD_IMM64(BPF_REG_2, y.b));
		emit(BPF_JMP_REG(spec.setup_signed ? BPF_JSLT : BPF_JLT,
				 BPF_REG_7, BPF_REG_1, JMP_TO(exit_pos)));
		emit(BPF_JMP_REG(spec.setup_signed ? BPF_JSGT : BPF_JGT,
				 BPF_REG_7, BPF_REG_2, JMP_TO(exit_pos)));
	}
	/* ; range test instruction
	 * if r6 <op> r7 goto +3; | if w6 <op> w7 goto +3;
	 */
	switch (op) {
	case OP_LT: op_code = spec.compare_signed ? BPF_JSLT : BPF_JLT; break;
	case OP_LE: op_code = spec.compare_signed ? BPF_JSLE : BPF_JLE; break;
	case OP_GT: op_code = spec.compare_signed ? BPF_JSGT : BPF_JGT; break;
	case OP_GE: op_code = spec.compare_signed ? BPF_JSGE : BPF_JGE; break;
	case OP_EQ: op_code = BPF_JEQ; break;
	case OP_NE: op_code = BPF_JNE; break;
	default:
		printf("unrecognized op %d\n", op);
		return -ENOTSUP;
	}
	/* ; BEFORE conditional, r0/w0 = {r6/w6,r7/w7} is to extract verifier state reliably
	 * ; this is used for debugging, as verifier doesn't always print
	 * ; registers states as of condition jump instruction (e.g., when
	 * ; precision marking happens)
	 * r0 = r6;               | w0 = w6;
	 * r0 = r7;               | w0 = w7;
	 */
	if (spec.compare_subregs) {
		emit(BPF_MOV32_REG(BPF_REG_0, BPF_REG_6));
		emit(BPF_MOV32_REG(BPF_REG_0, BPF_REG_7));
	} else {
		emit(BPF_MOV64_REG(BPF_REG_0, BPF_REG_6));
		emit(BPF_MOV64_REG(BPF_REG_0, BPF_REG_7));
	}
	if (spec.compare_subregs)
		emit(BPF_JMP32_REG(op_code, BPF_REG_6, BPF_REG_7, 3));
	else
		emit(BPF_JMP_REG(op_code, BPF_REG_6, BPF_REG_7, 3));
	/* ; FALSE branch, r0/w0 = {r6/w6,r7/w7} is to extract verifier state reliably
	 * r0 = r6;               | w0 = w6;
	 * r0 = r7;               | w0 = w7;
	 * exit;
	 */
	*false_pos = cur_pos;
	if (spec.compare_subregs) {
		emit(BPF_MOV32_REG(BPF_REG_0, BPF_REG_6));
		emit(BPF_MOV32_REG(BPF_REG_0, BPF_REG_7));
	} else {
		emit(BPF_MOV64_REG(BPF_REG_0, BPF_REG_6));
		emit(BPF_MOV64_REG(BPF_REG_0, BPF_REG_7));
	}
	if (branch_taken == 1) /* false branch is never taken */
		emit(BPF_EMIT_CALL(0xDEAD)); /* poison this branch */
	else
		emit(BPF_EXIT_INSN());
	/* ; TRUE branch, r0/w0 = {r6/w6,r7/w7} is to extract verifier state reliably
	 * r0 = r6;               | w0 = w6;
	 * r0 = r7;               | w0 = w7;
	 * exit;
	 */
	*true_pos = cur_pos;
	if (spec.compare_subregs) {
		emit(BPF_MOV32_REG(BPF_REG_0, BPF_REG_6));
		emit(BPF_MOV32_REG(BPF_REG_0, BPF_REG_7));
	} else {
		emit(BPF_MOV64_REG(BPF_REG_0, BPF_REG_6));
		emit(BPF_MOV64_REG(BPF_REG_0, BPF_REG_7));
	}
	if (branch_taken == 0) /* true branch is never taken */
		emit(BPF_EMIT_CALL(0xDEAD)); /* poison this branch */
	emit(BPF_EXIT_INSN()); /* last instruction has to be exit */

	fd = bpf_prog_load(BPF_PROG_TYPE_RAW_TRACEPOINT, "reg_bounds_test",
			   "GPL", insns, cur_pos, &opts);
	if (fd < 0)
		return fd;

	close(fd);
	return 0;
#undef emit
#undef JMP_TO
}

#define str_has_pfx(str, pfx) (strncmp(str, pfx, strlen(pfx)) == 0)

/* Parse register state from verifier log.
 * `s` should point to the start of "Rx = ..." substring in the verifier log.
 */
static int parse_reg_state(const char *s, struct reg_state *reg)
{
	/* There are two generic forms for SCALAR register:
	 * - known constant: R6_rwD=P%lld
	 * - range: R6_rwD=scalar(id=1,...), where "..." is a comma-separated
	 *   list of optional range specifiers:
	 *     - umin=%llu, if missing, assumed 0;
	 *     - umax=%llu, if missing, assumed U64_MAX;
	 *     - smin=%lld, if missing, assumed S64_MIN;
	 *     - smax=%lld, if missing, assumed S64_MAX;
	 *     - umin32=%d, if missing, assumed 0;
	 *     - umax32=%d, if missing, assumed U32_MAX;
	 *     - smin32=%d, if missing, assumed S32_MIN;
	 *     - smax32=%d, if missing, assumed S32_MAX;
	 *     - var_off=(%#llx; %#llx), tnum part, we don't care about it.
	 *
	 * If some of the values are equal, they will be grouped (but min/max
	 * are not mixed together, and similarly negative values are not
	 * grouped with non-negative ones). E.g.:
	 *
	 *   R6_w=Pscalar(smin=smin32=0, smax=umax=umax32=1000)
	 *
	 * _rwD part is optional (and any of the letters can be missing).
	 * P (precision mark) is optional as well.
	 *
	 * Anything inside scalar() is optional, including id, of course.
	 */
	struct {
		const char *pfx;
		u64 *dst, def;
		bool is_32, is_set;
	} *f, fields[8] = {
		{"smin=", &reg->r[S64].a, S64_MIN},
		{"smax=", &reg->r[S64].b, S64_MAX},
		{"umin=", &reg->r[U64].a, 0},
		{"umax=", &reg->r[U64].b, U64_MAX},
		{"smin32=", &reg->r[S32].a, (u32)S32_MIN, true},
		{"smax32=", &reg->r[S32].b, (u32)S32_MAX, true},
		{"umin32=", &reg->r[U32].a, 0,            true},
		{"umax32=", &reg->r[U32].b, U32_MAX,      true},
	};
	const char *p;
	int i;

	p = strchr(s, '=');
	if (!p)
		return -EINVAL;
	p++;
	if (*p == 'P')
		p++;

	if (!str_has_pfx(p, "scalar(")) {
		long long sval;
		enum num_t t;

		if (p[0] == '0' && p[1] == 'x') {
			if (sscanf(p, "%llx", &sval) != 1)
				return -EINVAL;
		} else {
			if (sscanf(p, "%lld", &sval) != 1)
				return -EINVAL;
		}

		reg->valid = true;
		for (t = first_t; t <= last_t; t++) {
			reg->r[t] = range(t, sval, sval);
		}
		return 0;
	}

	p += sizeof("scalar");
	while (p) {
		int midxs[ARRAY_SIZE(fields)], mcnt = 0;
		u64 val;

		for (i = 0; i < ARRAY_SIZE(fields); i++) {
			f = &fields[i];
			if (!str_has_pfx(p, f->pfx))
				continue;
			midxs[mcnt++] = i;
			p += strlen(f->pfx);
		}

		if (mcnt) {
			/* populate all matched fields */
			if (p[0] == '0' && p[1] == 'x') {
				if (sscanf(p, "%llx", &val) != 1)
					return -EINVAL;
			} else {
				if (sscanf(p, "%lld", &val) != 1)
					return -EINVAL;
			}

			for (i = 0; i < mcnt; i++) {
				f = &fields[midxs[i]];
				f->is_set = true;
				*f->dst = f->is_32 ? (u64)(u32)val : val;
			}
		} else if (str_has_pfx(p, "var_off")) {
			/* skip "var_off=(0x0; 0x3f)" part completely */
			p = strchr(p, ')');
			if (!p)
				return -EINVAL;
			p++;
		}

		p = strpbrk(p, ",)");
		if (*p == ')')
			break;
		if (p)
			p++;
	}

	reg->valid = true;

	for (i = 0; i < ARRAY_SIZE(fields); i++) {
		f = &fields[i];
		if (!f->is_set)
			*f->dst = f->def;
	}

	return 0;
}


/* Parse all register states (TRUE/FALSE branches and DST/SRC registers)
 * out of the verifier log for a corresponding test case BPF program.
 */
static int parse_range_cmp_log(const char *log_buf, struct case_spec spec,
			       int false_pos, int true_pos,
			       struct reg_state *false1_reg, struct reg_state *false2_reg,
			       struct reg_state *true1_reg, struct reg_state *true2_reg)
{
	struct {
		int insn_idx;
		int reg_idx;
		const char *reg_upper;
		struct reg_state *state;
	} specs[] = {
		{false_pos,     6, "R6=", false1_reg},
		{false_pos + 1, 7, "R7=", false2_reg},
		{true_pos,      6, "R6=", true1_reg},
		{true_pos + 1,  7, "R7=", true2_reg},
	};
	char buf[32];
	const char *p = log_buf, *q;
	int i, err;

	for (i = 0; i < 4; i++) {
		sprintf(buf, "%d: (%s) %s = %s%d", specs[i].insn_idx,
			spec.compare_subregs ? "bc" : "bf",
			spec.compare_subregs ? "w0" : "r0",
			spec.compare_subregs ? "w" : "r", specs[i].reg_idx);

		q = strstr(p, buf);
		if (!q) {
			*specs[i].state = (struct reg_state){.valid = false};
			continue;
		}
		p = strstr(q, specs[i].reg_upper);
		if (!p)
			return -EINVAL;
		err = parse_reg_state(p, specs[i].state);
		if (err)
			return -EINVAL;
	}
	return 0;
}

/* Validate ranges match, and print details if they don't */
static bool assert_range_eq(enum num_t t, struct range x, struct range y,
			    const char *ctx1, const char *ctx2)
{
	DEFINE_STRBUF(sb, 512);

	if (range_eq(x, y))
		return true;

	snappendf(sb, "MISMATCH %s.%s: ", ctx1, ctx2);
	snprintf_range(t, sb, x);
	snappendf(sb, " != ");
	snprintf_range(t, sb, y);

	printf("%s\n", sb->buf);

	return false;
}

/* Validate that register states match, and print details if they don't */
static bool assert_reg_state_eq(struct reg_state *r, struct reg_state *e, const char *ctx)
{
	bool ok = true;
	enum num_t t;

	if (r->valid != e->valid) {
		printf("MISMATCH %s: actual %s != expected %s\n", ctx,
		       r->valid ? "<valid>" : "<invalid>",
		       e->valid ? "<valid>" : "<invalid>");
		return false;
	}

	if (!r->valid)
		return true;

	for (t = first_t; t <= last_t; t++) {
		if (!assert_range_eq(t, r->r[t], e->r[t], ctx, t_str(t)))
			ok = false;
	}

	return ok;
}

/* Printf verifier log, filtering out irrelevant noise */
static void print_verifier_log(const char *buf)
{
	const char *p;

	while (buf[0]) {
		p = strchrnul(buf, '\n');

		/* filter out irrelevant precision backtracking logs */
		if (str_has_pfx(buf, "mark_precise: "))
			goto skip_line;

		printf("%.*s\n", (int)(p - buf), buf);

skip_line:
		buf = *p == '\0' ? p : p + 1;
	}
}

/* Simulate provided test case purely with our own range-based logic.
 * This is done to set up expectations for verifier's branch_taken logic and
 * verifier's register states in the verifier log.
 */
static void sim_case(enum num_t init_t, enum num_t cond_t,
		     struct range x, struct range y, enum op op,
		     struct reg_state *fr1, struct reg_state *fr2,
		     struct reg_state *tr1, struct reg_state *tr2,
		     int *branch_taken)
{
	const u64 A = x.a;
	const u64 B = x.b;
	const u64 C = y.a;
	const u64 D = y.b;
	struct reg_state rc;
	enum op rev_op = complement_op(op);
	enum num_t t;

	fr1->valid = fr2->valid = true;
	tr1->valid = tr2->valid = true;
	for (t = first_t; t <= last_t; t++) {
		/* if we are initializing using 32-bit subregisters,
		 * full registers get upper 32 bits zeroed automatically
		 */
		struct range z = t_is_32(init_t) ? unkn_subreg(t) : unkn[t];

		fr1->r[t] = fr2->r[t] = tr1->r[t] = tr2->r[t] = z;
	}

	/* step 1: r1 >= A, r2 >= C */
	reg_state_set_const(&rc, init_t, A);
	reg_state_cond(init_t, fr1, &rc, OP_GE, fr1, NULL, "r1>=A");
	reg_state_set_const(&rc, init_t, C);
	reg_state_cond(init_t, fr2, &rc, OP_GE, fr2, NULL, "r2>=C");
	*tr1 = *fr1;
	*tr2 = *fr2;
	if (env.verbosity >= VERBOSE_VERY) {
		printf("STEP1 (%s) R1: ", t_str(init_t)); print_reg_state(fr1, "\n");
		printf("STEP1 (%s) R2: ", t_str(init_t)); print_reg_state(fr2, "\n");
	}

	/* step 2: r1 <= B, r2 <= D */
	reg_state_set_const(&rc, init_t, B);
	reg_state_cond(init_t, fr1, &rc, OP_LE, fr1, NULL, "r1<=B");
	reg_state_set_const(&rc, init_t, D);
	reg_state_cond(init_t, fr2, &rc, OP_LE, fr2, NULL, "r2<=D");
	*tr1 = *fr1;
	*tr2 = *fr2;
	if (env.verbosity >= VERBOSE_VERY) {
		printf("STEP2 (%s) R1: ", t_str(init_t)); print_reg_state(fr1, "\n");
		printf("STEP2 (%s) R2: ", t_str(init_t)); print_reg_state(fr2, "\n");
	}

	/* step 3: r1 <op> r2 */
	*branch_taken = reg_state_branch_taken_op(cond_t, fr1, fr2, op);
	fr1->valid = fr2->valid = false;
	tr1->valid = tr2->valid = false;
	if (*branch_taken != 1) { /* FALSE is possible */
		fr1->valid = fr2->valid = true;
		reg_state_cond(cond_t, fr1, fr2, rev_op, fr1, fr2, "FALSE");
	}
	if (*branch_taken != 0) { /* TRUE is possible */
		tr1->valid = tr2->valid = true;
		reg_state_cond(cond_t, tr1, tr2, op, tr1, tr2, "TRUE");
	}
	if (env.verbosity >= VERBOSE_VERY) {
		printf("STEP3 (%s) FALSE R1:", t_str(cond_t)); print_reg_state(fr1, "\n");
		printf("STEP3 (%s) FALSE R2:", t_str(cond_t)); print_reg_state(fr2, "\n");
		printf("STEP3 (%s) TRUE  R1:", t_str(cond_t)); print_reg_state(tr1, "\n");
		printf("STEP3 (%s) TRUE  R2:", t_str(cond_t)); print_reg_state(tr2, "\n");
	}
}

/* ===============================
 * HIGH-LEVEL TEST CASE VALIDATION
 * ===============================
 */
static u32 upper_seeds[] = {
	0,
	1,
	U32_MAX,
	U32_MAX - 1,
	S32_MAX,
	(u32)S32_MIN,
};

static u32 lower_seeds[] = {
	0,
	1,
	2, (u32)-2,
	255, (u32)-255,
	UINT_MAX,
	UINT_MAX - 1,
	INT_MAX,
	(u32)INT_MIN,
};

struct ctx {
	int val_cnt, subval_cnt, range_cnt, subrange_cnt;
	u64 uvals[ARRAY_SIZE(upper_seeds) * ARRAY_SIZE(lower_seeds)];
	s64 svals[ARRAY_SIZE(upper_seeds) * ARRAY_SIZE(lower_seeds)];
	u32 usubvals[ARRAY_SIZE(lower_seeds)];
	s32 ssubvals[ARRAY_SIZE(lower_seeds)];
	struct range *uranges, *sranges;
	struct range *usubranges, *ssubranges;
	int max_failure_cnt, cur_failure_cnt;
	int total_case_cnt, case_cnt;
	int rand_case_cnt;
	unsigned rand_seed;
	__u64 start_ns;
	char progress_ctx[64];
};

static void cleanup_ctx(struct ctx *ctx)
{
	free(ctx->uranges);
	free(ctx->sranges);
	free(ctx->usubranges);
	free(ctx->ssubranges);
}

struct subtest_case {
	enum num_t init_t;
	enum num_t cond_t;
	struct range x;
	struct range y;
	enum op op;
};

static void subtest_case_str(struct strbuf *sb, struct subtest_case *t, bool use_op)
{
	snappendf(sb, "(%s)", t_str(t->init_t));
	snprintf_range(t->init_t, sb, t->x);
	snappendf(sb, " (%s)%s ", t_str(t->cond_t), use_op ? op_str(t->op) : "<op>");
	snprintf_range(t->init_t, sb, t->y);
}

/* Generate and validate test case based on specific combination of setup
 * register ranges (including their expected num_t domain), and conditional
 * operation to perform (including num_t domain in which it has to be
 * performed)
 */
static int verify_case_op(enum num_t init_t, enum num_t cond_t,
			  struct range x, struct range y, enum op op)
{
	char log_buf[256 * 1024];
	size_t log_sz = sizeof(log_buf);
	int err, false_pos = 0, true_pos = 0, branch_taken;
	struct reg_state fr1, fr2, tr1, tr2;
	struct reg_state fe1, fe2, te1, te2;
	bool failed = false;
	struct case_spec spec = {
		.init_subregs = (init_t == U32 || init_t == S32),
		.setup_subregs = (init_t == U32 || init_t == S32),
		.setup_signed = (init_t == S64 || init_t == S32),
		.compare_subregs = (cond_t == U32 || cond_t == S32),
		.compare_signed = (cond_t == S64 || cond_t == S32),
	};

	log_buf[0] = '\0';

	sim_case(init_t, cond_t, x, y, op, &fe1, &fe2, &te1, &te2, &branch_taken);

	err = load_range_cmp_prog(x, y, op, branch_taken, spec,
				  log_buf, log_sz, &false_pos, &true_pos);
	if (err) {
		ASSERT_OK(err, "load_range_cmp_prog");
		failed = true;
	}

	err = parse_range_cmp_log(log_buf, spec, false_pos, true_pos,
				  &fr1, &fr2, &tr1, &tr2);
	if (err) {
		ASSERT_OK(err, "parse_range_cmp_log");
		failed = true;
	}

	if (!assert_reg_state_eq(&fr1, &fe1, "false_reg1") ||
	    !assert_reg_state_eq(&fr2, &fe2, "false_reg2") ||
	    !assert_reg_state_eq(&tr1, &te1, "true_reg1") ||
	    !assert_reg_state_eq(&tr2, &te2, "true_reg2")) {
		failed = true;
	}

	if (failed || env.verbosity >= VERBOSE_NORMAL) {
		if (failed || env.verbosity >= VERBOSE_VERY) {
			printf("VERIFIER LOG:\n========================\n");
			print_verifier_log(log_buf);
			printf("=====================\n");
		}
		printf("ACTUAL   FALSE1: "); print_reg_state(&fr1, "\n");
		printf("EXPECTED FALSE1: "); print_reg_state(&fe1, "\n");
		printf("ACTUAL   FALSE2: "); print_reg_state(&fr2, "\n");
		printf("EXPECTED FALSE2: "); print_reg_state(&fe2, "\n");
		printf("ACTUAL   TRUE1:  "); print_reg_state(&tr1, "\n");
		printf("EXPECTED TRUE1:  "); print_reg_state(&te1, "\n");
		printf("ACTUAL   TRUE2:  "); print_reg_state(&tr2, "\n");
		printf("EXPECTED TRUE2:  "); print_reg_state(&te2, "\n");

		return failed ? -EINVAL : 0;
	}

	return 0;
}

/* Given setup ranges and number types, go over all supported operations,
 * generating individual subtest for each allowed combination
 */
static int verify_case_opt(struct ctx *ctx, enum num_t init_t, enum num_t cond_t,
			   struct range x, struct range y, bool is_subtest)
{
	DEFINE_STRBUF(sb, 256);
	int err;
	struct subtest_case sub = {
		.init_t = init_t,
		.cond_t = cond_t,
		.x = x,
		.y = y,
	};

	sb->pos = 0; /* reset position in strbuf */
	subtest_case_str(sb, &sub, false /* ignore op */);
	if (is_subtest && !test__start_subtest(sb->buf))
		return 0;

	for (sub.op = first_op; sub.op <= last_op; sub.op++) {
		sb->pos = 0; /* reset position in strbuf */
		subtest_case_str(sb, &sub, true /* print op */);

		if (env.verbosity >= VERBOSE_NORMAL) /* this speeds up debugging */
			printf("TEST CASE: %s\n", sb->buf);

		err = verify_case_op(init_t, cond_t, x, y, sub.op);
		if (err || env.verbosity >= VERBOSE_NORMAL)
			ASSERT_OK(err, sb->buf);
		if (err) {
			ctx->cur_failure_cnt++;
			if (ctx->cur_failure_cnt > ctx->max_failure_cnt)
				return err;
			return 0; /* keep testing other cases */
		}
		ctx->case_cnt++;
		if ((ctx->case_cnt % 10000) == 0) {
			double progress = (ctx->case_cnt + 0.0) / ctx->total_case_cnt;
			u64 elapsed_ns = get_time_ns() - ctx->start_ns;
			double remain_ns = elapsed_ns / progress * (1 - progress);

			fprintf(env.stderr_saved, "PROGRESS (%s): %d/%d (%.2lf%%), "
					    "elapsed %llu mins (%.2lf hrs), "
					    "ETA %.0lf mins (%.2lf hrs)\n",
				ctx->progress_ctx,
				ctx->case_cnt, ctx->total_case_cnt, 100.0 * progress,
				elapsed_ns / 1000000000 / 60,
				elapsed_ns / 1000000000.0 / 3600,
				remain_ns / 1000000000.0 / 60,
				remain_ns / 1000000000.0 / 3600);
		}
	}

	return 0;
}

static int verify_case(struct ctx *ctx, enum num_t init_t, enum num_t cond_t,
		       struct range x, struct range y)
{
	return verify_case_opt(ctx, init_t, cond_t, x, y, true /* is_subtest */);
}

/* ================================
 * GENERATED CASES FROM SEED VALUES
 * ================================
 */
static int u64_cmp(const void *p1, const void *p2)
{
	u64 x1 = *(const u64 *)p1, x2 = *(const u64 *)p2;

	return x1 != x2 ? (x1 < x2 ? -1 : 1) : 0;
}

static int u32_cmp(const void *p1, const void *p2)
{
	u32 x1 = *(const u32 *)p1, x2 = *(const u32 *)p2;

	return x1 != x2 ? (x1 < x2 ? -1 : 1) : 0;
}

static int s64_cmp(const void *p1, const void *p2)
{
	s64 x1 = *(const s64 *)p1, x2 = *(const s64 *)p2;

	return x1 != x2 ? (x1 < x2 ? -1 : 1) : 0;
}

static int s32_cmp(const void *p1, const void *p2)
{
	s32 x1 = *(const s32 *)p1, x2 = *(const s32 *)p2;

	return x1 != x2 ? (x1 < x2 ? -1 : 1) : 0;
}

/* Generate valid unique constants from seeds, both signed and unsigned */
static void gen_vals(struct ctx *ctx)
{
	int i, j, cnt = 0;

	for (i = 0; i < ARRAY_SIZE(upper_seeds); i++) {
		for (j = 0; j < ARRAY_SIZE(lower_seeds); j++) {
			ctx->uvals[cnt++] = (((u64)upper_seeds[i]) << 32) | lower_seeds[j];
		}
	}

	/* sort and compact uvals (i.e., it's `sort | uniq`) */
	qsort(ctx->uvals, cnt, sizeof(*ctx->uvals), u64_cmp);
	for (i = 1, j = 0; i < cnt; i++) {
		if (ctx->uvals[j] == ctx->uvals[i])
			continue;
		j++;
		ctx->uvals[j] = ctx->uvals[i];
	}
	ctx->val_cnt = j + 1;

	/* we have exactly the same number of s64 values, they are just in
	 * a different order than u64s, so just sort them differently
	 */
	for (i = 0; i < ctx->val_cnt; i++)
		ctx->svals[i] = ctx->uvals[i];
	qsort(ctx->svals, ctx->val_cnt, sizeof(*ctx->svals), s64_cmp);

	if (env.verbosity >= VERBOSE_SUPER) {
		DEFINE_STRBUF(sb1, 256);
		DEFINE_STRBUF(sb2, 256);

		for (i = 0; i < ctx->val_cnt; i++) {
			sb1->pos = sb2->pos = 0;
			snprintf_num(U64, sb1, ctx->uvals[i]);
			snprintf_num(S64, sb2, ctx->svals[i]);
			printf("SEED #%d: u64=%-20s s64=%-20s\n", i, sb1->buf, sb2->buf);
		}
	}

	/* 32-bit values are generated separately */
	cnt = 0;
	for (i = 0; i < ARRAY_SIZE(lower_seeds); i++) {
		ctx->usubvals[cnt++] = lower_seeds[i];
	}

	/* sort and compact usubvals (i.e., it's `sort | uniq`) */
	qsort(ctx->usubvals, cnt, sizeof(*ctx->usubvals), u32_cmp);
	for (i = 1, j = 0; i < cnt; i++) {
		if (ctx->usubvals[j] == ctx->usubvals[i])
			continue;
		j++;
		ctx->usubvals[j] = ctx->usubvals[i];
	}
	ctx->subval_cnt = j + 1;

	for (i = 0; i < ctx->subval_cnt; i++)
		ctx->ssubvals[i] = ctx->usubvals[i];
	qsort(ctx->ssubvals, ctx->subval_cnt, sizeof(*ctx->ssubvals), s32_cmp);

	if (env.verbosity >= VERBOSE_SUPER) {
		DEFINE_STRBUF(sb1, 256);
		DEFINE_STRBUF(sb2, 256);

		for (i = 0; i < ctx->subval_cnt; i++) {
			sb1->pos = sb2->pos = 0;
			snprintf_num(U32, sb1, ctx->usubvals[i]);
			snprintf_num(S32, sb2, ctx->ssubvals[i]);
			printf("SUBSEED #%d: u32=%-10s s32=%-10s\n", i, sb1->buf, sb2->buf);
		}
	}
}

/* Generate valid ranges from upper/lower seeds */
static int gen_ranges(struct ctx *ctx)
{
	int i, j, cnt = 0;

	for (i = 0; i < ctx->val_cnt; i++) {
		for (j = i; j < ctx->val_cnt; j++) {
			if (env.verbosity >= VERBOSE_SUPER) {
				DEFINE_STRBUF(sb1, 256);
				DEFINE_STRBUF(sb2, 256);

				sb1->pos = sb2->pos = 0;
				snprintf_range(U64, sb1, range(U64, ctx->uvals[i], ctx->uvals[j]));
				snprintf_range(S64, sb2, range(S64, ctx->svals[i], ctx->svals[j]));
				printf("RANGE #%d: u64=%-40s s64=%-40s\n", cnt, sb1->buf, sb2->buf);
			}
			cnt++;
		}
	}
	ctx->range_cnt = cnt;

	ctx->uranges = calloc(ctx->range_cnt, sizeof(*ctx->uranges));
	if (!ASSERT_OK_PTR(ctx->uranges, "uranges_calloc"))
		return -EINVAL;
	ctx->sranges = calloc(ctx->range_cnt, sizeof(*ctx->sranges));
	if (!ASSERT_OK_PTR(ctx->sranges, "sranges_calloc"))
		return -EINVAL;

	cnt = 0;
	for (i = 0; i < ctx->val_cnt; i++) {
		for (j = i; j < ctx->val_cnt; j++) {
			ctx->uranges[cnt] = range(U64, ctx->uvals[i], ctx->uvals[j]);
			ctx->sranges[cnt] = range(S64, ctx->svals[i], ctx->svals[j]);
			cnt++;
		}
	}

	cnt = 0;
	for (i = 0; i < ctx->subval_cnt; i++) {
		for (j = i; j < ctx->subval_cnt; j++) {
			if (env.verbosity >= VERBOSE_SUPER) {
				DEFINE_STRBUF(sb1, 256);
				DEFINE_STRBUF(sb2, 256);

				sb1->pos = sb2->pos = 0;
				snprintf_range(U32, sb1, range(U32, ctx->usubvals[i], ctx->usubvals[j]));
				snprintf_range(S32, sb2, range(S32, ctx->ssubvals[i], ctx->ssubvals[j]));
				printf("SUBRANGE #%d: u32=%-20s s32=%-20s\n", cnt, sb1->buf, sb2->buf);
			}
			cnt++;
		}
	}
	ctx->subrange_cnt = cnt;

	ctx->usubranges = calloc(ctx->subrange_cnt, sizeof(*ctx->usubranges));
	if (!ASSERT_OK_PTR(ctx->usubranges, "usubranges_calloc"))
		return -EINVAL;
	ctx->ssubranges = calloc(ctx->subrange_cnt, sizeof(*ctx->ssubranges));
	if (!ASSERT_OK_PTR(ctx->ssubranges, "ssubranges_calloc"))
		return -EINVAL;

	cnt = 0;
	for (i = 0; i < ctx->subval_cnt; i++) {
		for (j = i; j < ctx->subval_cnt; j++) {
			ctx->usubranges[cnt] = range(U32, ctx->usubvals[i], ctx->usubvals[j]);
			ctx->ssubranges[cnt] = range(S32, ctx->ssubvals[i], ctx->ssubvals[j]);
			cnt++;
		}
	}

	return 0;
}

static int parse_env_vars(struct ctx *ctx)
{
	const char *s;

	if ((s = getenv("REG_BOUNDS_MAX_FAILURE_CNT"))) {
		errno = 0;
		ctx->max_failure_cnt = strtol(s, NULL, 10);
		if (errno || ctx->max_failure_cnt < 0) {
			ASSERT_OK(-errno, "REG_BOUNDS_MAX_FAILURE_CNT");
			return -EINVAL;
		}
	}

	if ((s = getenv("REG_BOUNDS_RAND_CASE_CNT"))) {
		errno = 0;
		ctx->rand_case_cnt = strtol(s, NULL, 10);
		if (errno || ctx->rand_case_cnt < 0) {
			ASSERT_OK(-errno, "REG_BOUNDS_RAND_CASE_CNT");
			return -EINVAL;
		}
	}

	if ((s = getenv("REG_BOUNDS_RAND_SEED"))) {
		errno = 0;
		ctx->rand_seed = strtoul(s, NULL, 10);
		if (errno) {
			ASSERT_OK(-errno, "REG_BOUNDS_RAND_SEED");
			return -EINVAL;
		}
	}

	return 0;
}

static int prepare_gen_tests(struct ctx *ctx)
{
	const char *s;
	int err;

	if (!(s = getenv("SLOW_TESTS")) || strcmp(s, "1") != 0) {
		test__skip();
		return -ENOTSUP;
	}

	err = parse_env_vars(ctx);
	if (err)
		return err;

	gen_vals(ctx);
	err = gen_ranges(ctx);
	if (err) {
		ASSERT_OK(err, "gen_ranges");
		return err;
	}

	return 0;
}

/* Go over generated constants and ranges and validate various supported
 * combinations of them
 */
static void validate_gen_range_vs_const_64(enum num_t init_t, enum num_t cond_t)
{
	struct ctx ctx;
	struct range rconst;
	const struct range *ranges;
	const u64 *vals;
	int i, j;

	memset(&ctx, 0, sizeof(ctx));

	if (prepare_gen_tests(&ctx))
		goto cleanup;

	ranges = init_t == U64 ? ctx.uranges : ctx.sranges;
	vals = init_t == U64 ? ctx.uvals : (const u64 *)ctx.svals;

	ctx.total_case_cnt = (last_op - first_op + 1) * (2 * ctx.range_cnt * ctx.val_cnt);
	ctx.start_ns = get_time_ns();
	snprintf(ctx.progress_ctx, sizeof(ctx.progress_ctx),
		 "RANGE x CONST, %s -> %s",
		 t_str(init_t), t_str(cond_t));

	for (i = 0; i < ctx.val_cnt; i++) {
		for (j = 0; j < ctx.range_cnt; j++) {
			rconst = range(init_t, vals[i], vals[i]);

			/* (u64|s64)(<range> x <const>) */
			if (verify_case(&ctx, init_t, cond_t, ranges[j], rconst))
				goto cleanup;
			/* (u64|s64)(<const> x <range>) */
			if (verify_case(&ctx, init_t, cond_t, rconst, ranges[j]))
				goto cleanup;
		}
	}

cleanup:
	cleanup_ctx(&ctx);
}

static void validate_gen_range_vs_const_32(enum num_t init_t, enum num_t cond_t)
{
	struct ctx ctx;
	struct range rconst;
	const struct range *ranges;
	const u32 *vals;
	int i, j;

	memset(&ctx, 0, sizeof(ctx));

	if (prepare_gen_tests(&ctx))
		goto cleanup;

	ranges = init_t == U32 ? ctx.usubranges : ctx.ssubranges;
	vals = init_t == U32 ? ctx.usubvals : (const u32 *)ctx.ssubvals;

	ctx.total_case_cnt = (last_op - first_op + 1) * (2 * ctx.subrange_cnt * ctx.subval_cnt);
	ctx.start_ns = get_time_ns();
	snprintf(ctx.progress_ctx, sizeof(ctx.progress_ctx),
		 "RANGE x CONST, %s -> %s",
		 t_str(init_t), t_str(cond_t));

	for (i = 0; i < ctx.subval_cnt; i++) {
		for (j = 0; j < ctx.subrange_cnt; j++) {
			rconst = range(init_t, vals[i], vals[i]);

			/* (u32|s32)(<range> x <const>) */
			if (verify_case(&ctx, init_t, cond_t, ranges[j], rconst))
				goto cleanup;
			/* (u32|s32)(<const> x <range>) */
			if (verify_case(&ctx, init_t, cond_t, rconst, ranges[j]))
				goto cleanup;
		}
	}

cleanup:
	cleanup_ctx(&ctx);
}

static void validate_gen_range_vs_range(enum num_t init_t, enum num_t cond_t)
{
	struct ctx ctx;
	const struct range *ranges;
	int i, j, rcnt;

	memset(&ctx, 0, sizeof(ctx));

	if (prepare_gen_tests(&ctx))
		goto cleanup;

	switch (init_t)
	{
	case U64:
		ranges = ctx.uranges;
		rcnt = ctx.range_cnt;
		break;
	case U32:
		ranges = ctx.usubranges;
		rcnt = ctx.subrange_cnt;
		break;
	case S64:
		ranges = ctx.sranges;
		rcnt = ctx.range_cnt;
		break;
	case S32:
		ranges = ctx.ssubranges;
		rcnt = ctx.subrange_cnt;
		break;
	default:
		printf("validate_gen_range_vs_range!\n");
		exit(1);
	}

	ctx.total_case_cnt = (last_op - first_op + 1) * (2 * rcnt * (rcnt + 1) / 2);
	ctx.start_ns = get_time_ns();
	snprintf(ctx.progress_ctx, sizeof(ctx.progress_ctx),
		 "RANGE x RANGE, %s -> %s",
		 t_str(init_t), t_str(cond_t));

	for (i = 0; i < rcnt; i++) {
		for (j = i; j < rcnt; j++) {
			/* (<range> x <range>) */
			if (verify_case(&ctx, init_t, cond_t, ranges[i], ranges[j]))
				goto cleanup;
			if (verify_case(&ctx, init_t, cond_t, ranges[j], ranges[i]))
				goto cleanup;
		}
	}

cleanup:
	cleanup_ctx(&ctx);
}

/* Go over thousands of test cases generated from initial seed values.
 * Given this take a long time, guard this begind SLOW_TESTS=1 envvar. If
 * envvar is not set, this test is skipped during test_progs testing.
 *
 * We split this up into smaller subsets based on initialization and
 * conditional numeric domains to get an easy parallelization with test_progs'
 * -j argument.
 */

/* RANGE x CONST, U64 initial range */
void test_reg_bounds_gen_consts_u64_u64(void) { validate_gen_range_vs_const_64(U64, U64); }
void test_reg_bounds_gen_consts_u64_s64(void) { validate_gen_range_vs_const_64(U64, S64); }
void test_reg_bounds_gen_consts_u64_u32(void) { validate_gen_range_vs_const_64(U64, U32); }
void test_reg_bounds_gen_consts_u64_s32(void) { validate_gen_range_vs_const_64(U64, S32); }
/* RANGE x CONST, S64 initial range */
void test_reg_bounds_gen_consts_s64_u64(void) { validate_gen_range_vs_const_64(S64, U64); }
void test_reg_bounds_gen_consts_s64_s64(void) { validate_gen_range_vs_const_64(S64, S64); }
void test_reg_bounds_gen_consts_s64_u32(void) { validate_gen_range_vs_const_64(S64, U32); }
void test_reg_bounds_gen_consts_s64_s32(void) { validate_gen_range_vs_const_64(S64, S32); }
/* RANGE x CONST, U32 initial range */
void test_reg_bounds_gen_consts_u32_u64(void) { validate_gen_range_vs_const_32(U32, U64); }
void test_reg_bounds_gen_consts_u32_s64(void) { validate_gen_range_vs_const_32(U32, S64); }
void test_reg_bounds_gen_consts_u32_u32(void) { validate_gen_range_vs_const_32(U32, U32); }
void test_reg_bounds_gen_consts_u32_s32(void) { validate_gen_range_vs_const_32(U32, S32); }
/* RANGE x CONST, S32 initial range */
void test_reg_bounds_gen_consts_s32_u64(void) { validate_gen_range_vs_const_32(S32, U64); }
void test_reg_bounds_gen_consts_s32_s64(void) { validate_gen_range_vs_const_32(S32, S64); }
void test_reg_bounds_gen_consts_s32_u32(void) { validate_gen_range_vs_const_32(S32, U32); }
void test_reg_bounds_gen_consts_s32_s32(void) { validate_gen_range_vs_const_32(S32, S32); }

/* RANGE x RANGE, U64 initial range */
void test_reg_bounds_gen_ranges_u64_u64(void) { validate_gen_range_vs_range(U64, U64); }
void test_reg_bounds_gen_ranges_u64_s64(void) { validate_gen_range_vs_range(U64, S64); }
void test_reg_bounds_gen_ranges_u64_u32(void) { validate_gen_range_vs_range(U64, U32); }
void test_reg_bounds_gen_ranges_u64_s32(void) { validate_gen_range_vs_range(U64, S32); }
/* RANGE x RANGE, S64 initial range */
void test_reg_bounds_gen_ranges_s64_u64(void) { validate_gen_range_vs_range(S64, U64); }
void test_reg_bounds_gen_ranges_s64_s64(void) { validate_gen_range_vs_range(S64, S64); }
void test_reg_bounds_gen_ranges_s64_u32(void) { validate_gen_range_vs_range(S64, U32); }
void test_reg_bounds_gen_ranges_s64_s32(void) { validate_gen_range_vs_range(S64, S32); }
/* RANGE x RANGE, U32 initial range */
void test_reg_bounds_gen_ranges_u32_u64(void) { validate_gen_range_vs_range(U32, U64); }
void test_reg_bounds_gen_ranges_u32_s64(void) { validate_gen_range_vs_range(U32, S64); }
void test_reg_bounds_gen_ranges_u32_u32(void) { validate_gen_range_vs_range(U32, U32); }
void test_reg_bounds_gen_ranges_u32_s32(void) { validate_gen_range_vs_range(U32, S32); }
/* RANGE x RANGE, S32 initial range */
void test_reg_bounds_gen_ranges_s32_u64(void) { validate_gen_range_vs_range(S32, U64); }
void test_reg_bounds_gen_ranges_s32_s64(void) { validate_gen_range_vs_range(S32, S64); }
void test_reg_bounds_gen_ranges_s32_u32(void) { validate_gen_range_vs_range(S32, U32); }
void test_reg_bounds_gen_ranges_s32_s32(void) { validate_gen_range_vs_range(S32, S32); }

#define DEFAULT_RAND_CASE_CNT 100

#define RAND_21BIT_MASK ((1 << 22) - 1)

static u64 rand_u64()
{
	/* RAND_MAX is guaranteed to be at least 1<<15, but in practice it
	 * seems to be 1<<31, so we need to call it thrice to get full u64;
	 * we'll use roughly equal split: 22 + 21 + 21 bits
	 */
	return ((u64)random() << 42) |
	       (((u64)random() & RAND_21BIT_MASK) << 21) |
	       (random() & RAND_21BIT_MASK);
}

static u64 rand_const(enum num_t t)
{
	return cast_t(t, rand_u64());
}

static struct range rand_range(enum num_t t)
{
	u64 x = rand_const(t), y = rand_const(t);

	return range(t, min_t(t, x, y), max_t(t, x, y));
}

static void validate_rand_ranges(enum num_t init_t, enum num_t cond_t, bool const_range)
{
	struct ctx ctx;
	struct range range1, range2;
	int err, i;
	u64 t;

	memset(&ctx, 0, sizeof(ctx));

	err = parse_env_vars(&ctx);
	if (err) {
		ASSERT_OK(err, "parse_env_vars");
		return;
	}

	if (ctx.rand_case_cnt == 0)
		ctx.rand_case_cnt = DEFAULT_RAND_CASE_CNT;
	if (ctx.rand_seed == 0)
		ctx.rand_seed = (unsigned)get_time_ns();

	srandom(ctx.rand_seed);

	ctx.total_case_cnt = (last_op - first_op + 1) * (2 * ctx.rand_case_cnt);
	ctx.start_ns = get_time_ns();
	snprintf(ctx.progress_ctx, sizeof(ctx.progress_ctx),
		 "[RANDOM SEED %u] RANGE x %s, %s -> %s",
		 ctx.rand_seed, const_range ? "CONST" : "RANGE",
		 t_str(init_t), t_str(cond_t));

	for (i = 0; i < ctx.rand_case_cnt; i++) {
		range1 = rand_range(init_t);
		if (const_range) {
			t = rand_const(init_t);
			range2 = range(init_t, t, t);
		} else {
			range2 = rand_range(init_t);
		}

		/* <range1> x <range2> */
		if (verify_case_opt(&ctx, init_t, cond_t, range1, range2, false /* !is_subtest */))
			goto cleanup;
		/* <range2> x <range1> */
		if (verify_case_opt(&ctx, init_t, cond_t, range2, range1, false /* !is_subtest */))
			goto cleanup;
	}

cleanup:
	/* make sure we report random seed for reproducing */
	ASSERT_TRUE(true, ctx.progress_ctx);
	cleanup_ctx(&ctx);
}

/* [RANDOM] RANGE x CONST, U64 initial range */
void test_reg_bounds_rand_consts_u64_u64(void) { validate_rand_ranges(U64, U64, true /* const */); }
void test_reg_bounds_rand_consts_u64_s64(void) { validate_rand_ranges(U64, S64, true /* const */); }
void test_reg_bounds_rand_consts_u64_u32(void) { validate_rand_ranges(U64, U32, true /* const */); }
void test_reg_bounds_rand_consts_u64_s32(void) { validate_rand_ranges(U64, S32, true /* const */); }
/* [RANDOM] RANGE x CONST, S64 initial range */
void test_reg_bounds_rand_consts_s64_u64(void) { validate_rand_ranges(S64, U64, true /* const */); }
void test_reg_bounds_rand_consts_s64_s64(void) { validate_rand_ranges(S64, S64, true /* const */); }
void test_reg_bounds_rand_consts_s64_u32(void) { validate_rand_ranges(S64, U32, true /* const */); }
void test_reg_bounds_rand_consts_s64_s32(void) { validate_rand_ranges(S64, S32, true /* const */); }
/* [RANDOM] RANGE x CONST, U32 initial range */
void test_reg_bounds_rand_consts_u32_u64(void) { validate_rand_ranges(U32, U64, true /* const */); }
void test_reg_bounds_rand_consts_u32_s64(void) { validate_rand_ranges(U32, S64, true /* const */); }
void test_reg_bounds_rand_consts_u32_u32(void) { validate_rand_ranges(U32, U32, true /* const */); }
void test_reg_bounds_rand_consts_u32_s32(void) { validate_rand_ranges(U32, S32, true /* const */); }
/* [RANDOM] RANGE x CONST, S32 initial range */
void test_reg_bounds_rand_consts_s32_u64(void) { validate_rand_ranges(S32, U64, true /* const */); }
void test_reg_bounds_rand_consts_s32_s64(void) { validate_rand_ranges(S32, S64, true /* const */); }
void test_reg_bounds_rand_consts_s32_u32(void) { validate_rand_ranges(S32, U32, true /* const */); }
void test_reg_bounds_rand_consts_s32_s32(void) { validate_rand_ranges(S32, S32, true /* const */); }

/* [RANDOM] RANGE x RANGE, U64 initial range */
void test_reg_bounds_rand_ranges_u64_u64(void) { validate_rand_ranges(U64, U64, false /* range */); }
void test_reg_bounds_rand_ranges_u64_s64(void) { validate_rand_ranges(U64, S64, false /* range */); }
void test_reg_bounds_rand_ranges_u64_u32(void) { validate_rand_ranges(U64, U32, false /* range */); }
void test_reg_bounds_rand_ranges_u64_s32(void) { validate_rand_ranges(U64, S32, false /* range */); }
/* [RANDOM] RANGE x RANGE, S64 initial range */
void test_reg_bounds_rand_ranges_s64_u64(void) { validate_rand_ranges(S64, U64, false /* range */); }
void test_reg_bounds_rand_ranges_s64_s64(void) { validate_rand_ranges(S64, S64, false /* range */); }
void test_reg_bounds_rand_ranges_s64_u32(void) { validate_rand_ranges(S64, U32, false /* range */); }
void test_reg_bounds_rand_ranges_s64_s32(void) { validate_rand_ranges(S64, S32, false /* range */); }
/* [RANDOM] RANGE x RANGE, U32 initial range */
void test_reg_bounds_rand_ranges_u32_u64(void) { validate_rand_ranges(U32, U64, false /* range */); }
void test_reg_bounds_rand_ranges_u32_s64(void) { validate_rand_ranges(U32, S64, false /* range */); }
void test_reg_bounds_rand_ranges_u32_u32(void) { validate_rand_ranges(U32, U32, false /* range */); }
void test_reg_bounds_rand_ranges_u32_s32(void) { validate_rand_ranges(U32, S32, false /* range */); }
/* [RANDOM] RANGE x RANGE, S32 initial range */
void test_reg_bounds_rand_ranges_s32_u64(void) { validate_rand_ranges(S32, U64, false /* range */); }
void test_reg_bounds_rand_ranges_s32_s64(void) { validate_rand_ranges(S32, S64, false /* range */); }
void test_reg_bounds_rand_ranges_s32_u32(void) { validate_rand_ranges(S32, U32, false /* range */); }
void test_reg_bounds_rand_ranges_s32_s32(void) { validate_rand_ranges(S32, S32, false /* range */); }

/* A set of hard-coded "interesting" cases to validate as part of normal
 * test_progs test runs
 */
static struct subtest_case crafted_cases[] = {
	{U64, U64, {0, 0xffffffff}, {0, 0}},
	{U64, U64, {0, 0x80000000}, {0, 0}},
	{U64, U64, {0x100000000ULL, 0x100000100ULL}, {0, 0}},
	{U64, U64, {0x100000000ULL, 0x180000000ULL}, {0, 0}},
	{U64, U64, {0x100000000ULL, 0x1ffffff00ULL}, {0, 0}},
	{U64, U64, {0x100000000ULL, 0x1ffffff01ULL}, {0, 0}},
	{U64, U64, {0x100000000ULL, 0x1fffffffeULL}, {0, 0}},
	{U64, U64, {0x100000001ULL, 0x1000000ffULL}, {0, 0}},

	/* single point overlap, interesting BPF_EQ and BPF_NE interactions */
	{U64, U64, {0, 1}, {1, 0x80000000}},
	{U64, S64, {0, 1}, {1, 0x80000000}},
	{U64, U32, {0, 1}, {1, 0x80000000}},
	{U64, S32, {0, 1}, {1, 0x80000000}},

	{U64, S64, {0, 0xffffffff00000000ULL}, {0, 0}},
	{U64, S64, {0x7fffffffffffffffULL, 0xffffffff00000000ULL}, {0, 0}},
	{U64, S64, {0x7fffffff00000001ULL, 0xffffffff00000000ULL}, {0, 0}},
	{U64, S64, {0, 0xffffffffULL}, {1, 1}},
	{U64, S64, {0, 0xffffffffULL}, {0x7fffffff, 0x7fffffff}},

	{U64, U32, {0, 0x100000000}, {0, 0}},
	{U64, U32, {0xfffffffe, 0x100000000}, {0x80000000, 0x80000000}},

	{U64, S32, {0, 0xffffffff00000000ULL}, {0, 0}},
	/* these are tricky cases where lower 32 bits allow to tighten 64
	 * bit boundaries based on tightened lower 32 bit boundaries
	 */
	{U64, S32, {0, 0x0ffffffffULL}, {0, 0}},
	{U64, S32, {0, 0x100000000ULL}, {0, 0}},
	{U64, S32, {0, 0x100000001ULL}, {0, 0}},
	{U64, S32, {0, 0x180000000ULL}, {0, 0}},
	{U64, S32, {0, 0x17fffffffULL}, {0, 0}},
	{U64, S32, {0, 0x180000001ULL}, {0, 0}},

	/* verifier knows about [-1, 0] range for s32 for this case already */
	{S64, S64, {0xffffffffffffffffULL, 0}, {0xffffffff00000000ULL, 0xffffffff00000000ULL}},
	/* but didn't know about these cases initially */
	{U64, U64, {0xffffffff, 0x100000000ULL}, {0, 0}}, /* s32: [-1, 0] */
	{U64, U64, {0xffffffff, 0x100000001ULL}, {0, 0}}, /* s32: [-1, 1] */

	/* longer convergence case: learning from u64 -> s64 -> u64 -> u32,
	 * arriving at u32: [1, U32_MAX] (instead of more pessimistic [0, U32_MAX])
	 */
	{S64, U64, {0xffffffff00000001ULL, 0}, {0xffffffff00000000ULL, 0xffffffff00000000ULL}},

	{U32, U32, {1, U32_MAX}, {0, 0}},

	{U32, S32, {0, U32_MAX}, {U32_MAX, U32_MAX}},

	{S32, U64, {(u32)S32_MIN, (u32)S32_MIN}, {(u32)(s32)-255, 0}},
	{S32, S64, {(u32)S32_MIN, (u32)(s32)-255}, {(u32)(s32)-2, 0}},
	{S32, S64, {0, 1}, {(u32)S32_MIN, (u32)S32_MIN}},
	{S32, U32, {(u32)S32_MIN, (u32)S32_MIN}, {(u32)S32_MIN, (u32)S32_MIN}},

	/* edge overlap testings for BPF_NE */
	{U64, U64, {0, U64_MAX}, {U64_MAX, U64_MAX}},
	{U64, U64, {0, U64_MAX}, {0, 0}},
	{S64, U64, {S64_MIN, 0}, {S64_MIN, S64_MIN}},
	{S64, U64, {S64_MIN, 0}, {0, 0}},
	{S64, U64, {S64_MIN, S64_MAX}, {S64_MAX, S64_MAX}},
	{U32, U32, {0, U32_MAX}, {0, 0}},
	{U32, U32, {0, U32_MAX}, {U32_MAX, U32_MAX}},
	{S32, U32, {(u32)S32_MIN, 0}, {0, 0}},
	{S32, U32, {(u32)S32_MIN, 0}, {(u32)S32_MIN, (u32)S32_MIN}},
	{S32, U32, {(u32)S32_MIN, S32_MAX}, {S32_MAX, S32_MAX}},
	{S64, U32, {0x0, 0x1f}, {0xffffffff80000000ULL, 0x000000007fffffffULL}},
	{S64, U32, {0x0, 0x1f}, {0xffffffffffff8000ULL, 0x0000000000007fffULL}},
	{S64, U32, {0x0, 0x1f}, {0xffffffffffffff80ULL, 0x000000000000007fULL}},
};

/* Go over crafted hard-coded cases. This is fast, so we do it as part of
 * normal test_progs run.
 */
void test_reg_bounds_crafted(void)
{
	struct ctx ctx;
	int i;

	memset(&ctx, 0, sizeof(ctx));

	for (i = 0; i < ARRAY_SIZE(crafted_cases); i++) {
		struct subtest_case *c = &crafted_cases[i];

		verify_case(&ctx, c->init_t, c->cond_t, c->x, c->y);
		verify_case(&ctx, c->init_t, c->cond_t, c->y, c->x);
	}

	cleanup_ctx(&ctx);
}
