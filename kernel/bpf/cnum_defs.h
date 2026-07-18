/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#ifndef T
#error "Define T (bit width: 32, 64) before including cnum_defs.h"
#endif

#include <linux/cnum.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/minmax.h>
#include <linux/compiler_types.h>

#define cnum_t   __PASTE(cnum, T)
#define ut       __PASTE(u, T)
#define st       __PASTE(s, T)
#define UT_MAX   __PASTE(__PASTE(U, T), _MAX)
#define ST_MAX   __PASTE(__PASTE(S, T), _MAX)
#define ST_MIN   __PASTE(__PASTE(S, T), _MIN)
#define EMPTY    __PASTE(__PASTE(CNUM, T), _EMPTY)
#define FN(name) __PASTE(__PASTE(cnum, T), __PASTE(_, name))

struct cnum_t FN(from_urange)(ut min, ut max)
{
	return (struct cnum_t){ .base = min, .size = (ut)max - min };
}

struct cnum_t FN(from_srange)(st min, st max)
{
	ut size = (ut)max - (ut)min;
	ut base = size == UT_MAX ? 0 : (ut)min;

	return (struct cnum_t){ .base = base, .size = size };
}

/* True if this cnum represents two unsigned ranges. */
static inline bool FN(urange_overflow)(struct cnum_t cnum)
{
	/* Same as cnum.base + cnum.size > UT_MAX but avoids overflow */
	return cnum.size > UT_MAX - (ut)cnum.base;
}

/*
 * cnum{T}_umin / cnum{T}_umax query an unsigned range represented by this cnum.
 * If cnum represents a range crossing the UT_MAX/0 boundary, the unbound range
 * [0..UT_MAX] is returned.
 */
ut FN(umin)(struct cnum_t cnum)
{
	return FN(urange_overflow)(cnum) ? 0 : cnum.base;
}
EXPORT_SYMBOL_GPL(FN(umin));

ut FN(umax)(struct cnum_t cnum)
{
	return FN(urange_overflow)(cnum) ? UT_MAX : cnum.base + cnum.size;
}
EXPORT_SYMBOL_GPL(FN(umax));

/* True if this cnum represents two signed ranges. */
static inline bool FN(srange_overflow)(struct cnum_t cnum)
{
	return FN(contains)(cnum, (ut)ST_MAX) && FN(contains)(cnum, (ut)ST_MIN);
}

/*
 * cnum{T}_smin / cnum{T}_smax query a signed range represented by this cnum.
 * If cnum represents a range crossing the ST_MAX/ST_MIN boundary, the unbound range
 * [ST_MIN..ST_MAX] is returned.
 */
st FN(smin)(struct cnum_t cnum)
{
	return FN(srange_overflow)(cnum)
	       ? ST_MIN
	       : min((st)cnum.base, (st)(cnum.base + cnum.size));
}

st FN(smax)(struct cnum_t cnum)
{
	return FN(srange_overflow)(cnum)
	       ? ST_MAX
	       : max((st)cnum.base, (st)(cnum.base + cnum.size));
}

/*
 * Returns a possibly empty intersection of cnums 'a' and 'b'.
 * If 'a' and 'b' intersect in two sub-arcs, the function over-approximates
 * and returns either 'a' or 'b', whichever is smaller.
 */
struct cnum_t FN(intersect)(struct cnum_t a, struct cnum_t b)
{
	struct cnum_t b1;
	ut dbase;

	if (FN(is_empty)(a) || FN(is_empty)(b))
		return EMPTY;

	if (a.base > b.base)
		swap(a, b);

	/*
	 * Rotate frame of reference such that a.base is 0.
	 * 'b1' is 'b' in this frame of reference.
	 */
	dbase = b.base - a.base;
	b1 = (struct cnum_t){ dbase, b.size };
	if (FN(urange_overflow)(b1)) {
		if (b1.base <= a.size) {
			/*
			 * Rotated frame (a.base at origin):
			 *
			 * 0                                       UT_MAX
			 * |--------------------------------------------|
			 * [=== a ==========================]           |
			 * [= b1 tail =]  [========= b1 main ==========>]
			 *                 ^-- b1.base <= a.size
			 *
			 * 'a' and 'b' intersect in two disjoint arcs,
			 * can't represent as single cnum, over-approximate
			 * the result.
			 */
			return a.size <= b.size ? a : b;
		} else {
			/*
			 * Rotated frame (a.base at origin):
			 *
			 * 0                                       UT_MAX
			 * |--------------------------------------------|
			 * [=== a =============]  |                     |
			 * [= b1 tail =]          [======= b1 main ====>]
			 *                         ^-- b1.base > a.size
			 *
			 * Only 'b' tail intersects 'a'.
			 */
			return (struct cnum_t) {
				.base = a.base,
				.size = min(a.size, (ut)(b1.base + b1.size)),
			};
		}
	} else if (a.size >= b1.base) {
		/*
		 * Rotated frame (a.base at origin):
		 *
		 * 0                                             UT_MAX
		 * |--------------------------------------------------|
		 * [=== a ==================================]         |
		 *                   [== b1 =====================]
		 *
		 * 0                                             UT_MAX
		 * |--------------------------------------------------|
		 * [=== a ==================================]         |
		 *                   [== b1 ====]
		 *                   ^-- b1.base <= a.size
		 *                   |<-- a.size - dbase -->|
		 *
		 * 'a' and 'b' intersect as one cnum.
		 */
		return (struct cnum_t) {
			.base = b.base,
			.size = min((ut)(a.size - dbase), b.size),
		};
	} else {
		return EMPTY;
	}
}

void FN(intersect_with)(struct cnum_t *dst, struct cnum_t src)
{
	*dst = FN(intersect)(*dst, src);
}

void FN(intersect_with_urange)(struct cnum_t *dst, ut min, ut max)
{
	FN(intersect_with)(dst, FN(from_urange)(min, max));
}

void FN(intersect_with_srange)(struct cnum_t *dst, st min, st max)
{
	FN(intersect_with)(dst, FN(from_srange)(min, max));
}

static inline struct cnum_t FN(normalize)(struct cnum_t cnum)
{
	if (cnum.size == UT_MAX && cnum.base != 0 && cnum.base != (ut)ST_MAX)
		cnum.base = 0;
	return cnum;
}

struct cnum_t FN(add)(struct cnum_t a, struct cnum_t b)
{
	if (FN(is_empty)(a) || FN(is_empty)(b))
		return EMPTY;
	if (a.size > UT_MAX - b.size)
		return (struct cnum_t){ 0, (ut)UT_MAX };
	else
		return FN(normalize)((struct cnum_t){ a.base + b.base, a.size + b.size });
}

struct cnum_t FN(negate)(struct cnum_t a)
{
	if (FN(is_empty)(a))
		return EMPTY;
	return FN(normalize)((struct cnum_t){ -((ut)a.base + a.size), a.size });
}

bool FN(is_empty)(struct cnum_t cnum)
{
	return cnum.base == EMPTY.base && cnum.size == EMPTY.size;
}

bool FN(contains)(struct cnum_t cnum, ut v)
{
	if (FN(is_empty)(cnum))
		return false;
	if (FN(urange_overflow)(cnum))
		return v >= cnum.base || v <= (ut)cnum.base + cnum.size;
	else
		return v >= cnum.base && v <= (ut)cnum.base + cnum.size;
}

bool FN(is_const)(struct cnum_t cnum)
{
	return cnum.size == 0;
}

bool FN(is_subset)(struct cnum_t bigger, struct cnum_t smaller)
{
	if (FN(is_empty(smaller)))
		return true;
	if (FN(is_empty(bigger)))
		return false;
	/* rotate both arcs such that 'bigger' starts at origin, hence does not overflow */
	smaller.base -= bigger.base;
	bigger.base = 0;
	if (FN(urange_overflow)(smaller) && bigger.size < UT_MAX)
		return false;
	return smaller.base + smaller.size <= bigger.size;
}

#undef EMPTY
#undef cnum_t
#undef ut
#undef st
#undef UT_MAX
#undef ST_MAX
#undef ST_MIN
#undef FN
