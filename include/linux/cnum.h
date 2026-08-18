/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#ifndef _LINUX_CNUM_H
#define _LINUX_CNUM_H

#include <linux/types.h>

/*
 * cnum32: a circular number.
 * A unified representation for signed and unsigned ranges.
 *
 * Assume that a 32-bit range is a circle, with 0 being in the 12 o'clock
 * position, numbers placed sequentially in clockwise order and U32_MAX
 * in the 11 o'clock position. Signed values map onto the same circle:
 * S32_MAX sits at 5 o'clock, S32_MIN sits at 6 o'clock (opposite 0),
 * negative values occupy the left half and positive values the right half.
 *
 * @cnum32 represents an arc on this circle drawn clockwise.
 * @base corresponds to the first value of the range.
 * @size corresponds to the number of integers in the range excluding @base.
 * (The @base is excluded to avoid integer overflow when representing the full
 *  0..U32_MAX range, which corresponds to 2^32, which can't be stored in u32).
 *
 * For example: {U32_MAX, 1} corresponds to signed range [-1, 0],
 *              {S32_MAX, 1} corresponds to unsigned range [S32_MAX, S32_MIN].
 */
struct cnum32 {
	u32 base;
	u32 size;
};

#define CNUM32_UNBOUNDED ((struct cnum32){ .base = 0, .size = U32_MAX })
#define CNUM32_EMPTY ((struct cnum32){ .base = U32_MAX, .size = U32_MAX })

struct cnum32 cnum32_from_urange(u32 min, u32 max);
struct cnum32 cnum32_from_srange(s32 min, s32 max);
u32 cnum32_umin(struct cnum32 cnum);
u32 cnum32_umax(struct cnum32 cnum);
s32 cnum32_smin(struct cnum32 cnum);
s32 cnum32_smax(struct cnum32 cnum);
struct cnum32 cnum32_intersect(struct cnum32 a, struct cnum32 b);
void cnum32_intersect_with(struct cnum32 *dst, struct cnum32 src);
void cnum32_intersect_with_urange(struct cnum32 *dst, u32 min, u32 max);
void cnum32_intersect_with_srange(struct cnum32 *dst, s32 min, s32 max);
bool cnum32_contains(struct cnum32 cnum, u32 v);
bool cnum32_is_const(struct cnum32 cnum);
bool cnum32_is_empty(struct cnum32 cnum);
struct cnum32 cnum32_add(struct cnum32 a, struct cnum32 b);
struct cnum32 cnum32_negate(struct cnum32 a);
bool cnum32_is_subset(struct cnum32 outer, struct cnum32 inner);

/* Same as cnum32 but for 64-bit ranges */
struct cnum64 {
	u64 base;
	u64 size;
};

#define CNUM64_UNBOUNDED ((struct cnum64){ .base = 0, .size = U64_MAX })
#define CNUM64_EMPTY ((struct cnum64){ .base = U64_MAX, .size = U64_MAX })

struct cnum64 cnum64_from_urange(u64 min, u64 max);
struct cnum64 cnum64_from_srange(s64 min, s64 max);
u64 cnum64_umin(struct cnum64 cnum);
u64 cnum64_umax(struct cnum64 cnum);
s64 cnum64_smin(struct cnum64 cnum);
s64 cnum64_smax(struct cnum64 cnum);
struct cnum64 cnum64_intersect(struct cnum64 a, struct cnum64 b);
void cnum64_intersect_with(struct cnum64 *dst, struct cnum64 src);
void cnum64_intersect_with_urange(struct cnum64 *dst, u64 min, u64 max);
void cnum64_intersect_with_srange(struct cnum64 *dst, s64 min, s64 max);
bool cnum64_contains(struct cnum64 cnum, u64 v);
bool cnum64_is_const(struct cnum64 cnum);
bool cnum64_is_empty(struct cnum64 cnum);
struct cnum64 cnum64_add(struct cnum64 a, struct cnum64 b);
struct cnum64 cnum64_negate(struct cnum64 a);
bool cnum64_is_subset(struct cnum64 outer, struct cnum64 inner);

struct cnum32 cnum32_from_cnum64(struct cnum64 cnum);
struct cnum64 cnum64_cnum32_intersect(struct cnum64 a, struct cnum32 b);

#endif /* _LINUX_CNUM_H */
