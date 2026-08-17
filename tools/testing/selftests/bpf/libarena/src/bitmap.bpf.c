// SPDX-License-Identifier: LGPL-2.1 OR BSD-2-Clause
/*
 * Copyright (c) 2025-2026 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2025-2026 Emil Tsalapatis <emil@etsalapatis.com>
 */

#include <libarena/common.h>

#include <libarena/asan.h>
#include <libarena/bitmap.h>

__weak
struct arena_bitmap __arena *bmp_alloc(size_t bits)
{
	struct arena_bitmap __arena *bmp;
	size_t size = BITS_TO_LONG_LONGS(bits) * sizeof(bmp->bits[0]);

	/* Assume long-aligned masks. */
	if (bits % BITS_PER_LONG_LONG)
		return NULL;

	bmp = (struct arena_bitmap __arena *)arena_malloc(size);
	if (!bmp)
		return NULL;

	bmp_clear(bits, bmp);

	return bmp;
}

__weak
void bmp_free(struct arena_bitmap __arena *bmp)
{
	arena_free(bmp);
}

__weak
void __bmp_set_bit(u32 bit, struct arena_bitmap __arena *bmp)
{
	bmp->bits[BIT_WORD(bit)] |= BIT_MASK(bit);
}

__weak
void __bmp_clear_bit(u32 bit, struct arena_bitmap __arena *bmp)
{
	bmp->bits[BIT_WORD(bit)] &= ~BIT_MASK(bit);
}

__weak
bool bmp_test_bit(u32 bit, struct arena_bitmap __arena *bmp)
{
	return bmp->bits[BIT_WORD(bit)] & BIT_MASK(bit);
}

__weak
bool bmp_test_and_clear_bit(u32 bit, struct arena_bitmap __arena *bmp)
{
	u64 val = BIT_MASK(bit);
	u32 idx = BIT_WORD(bit);
	u64 old, new, actual;

	do {
		old = bmp->bits[idx];

		if (!(old & val))
			return false;

		new = old & ~val;
		actual = cmpxchg(&bmp->bits[idx], old, new);

		if (actual == old)
			return true;

	} while (can_loop);

	return false;
}

__weak
bool bmp_test_and_set_bit(u32 bit, struct arena_bitmap __arena *bmp)
{
	u64 val = BIT_MASK(bit);
	u32 idx = BIT_WORD(bit);
	u64 old, new, actual;

	do {
		old = bmp->bits[idx];

		if ((old & val))
			return true;

		new = old | val;
		actual = cmpxchg(&bmp->bits[idx], old, new);

		if (actual == old)
			return false;

	} while (can_loop);

	return false;
}

__weak
void bmp_clear_bit(u32 bit, struct arena_bitmap __arena *bmp)
{
	u64 val = BIT_MASK(bit);
	u32 idx = BIT_WORD(bit);
	u64 old, new, actual;

	do {
		old = bmp->bits[idx];
		new = old & ~val;
		actual = cmpxchg(&bmp->bits[idx], old, new);

	} while (actual != old && can_loop);
}

__weak
void bmp_set_bit(u32 bit, struct arena_bitmap __arena *bmp)
{
	u64 val = BIT_MASK(bit);
	u32 idx = BIT_WORD(bit);
	u64 old, new, actual;

	do {
		old = bmp->bits[idx];
		new = old | val;
		actual = cmpxchg(&bmp->bits[idx], old, new);

	} while (actual != old && can_loop);
}

__weak
void bmp_clear(size_t bits, struct arena_bitmap __arena *bmp)
{
	size_t nwords = BITS_TO_LONG_LONGS(bits);
	volatile u32 i;

	for (i = zero; i < nwords && can_loop; i++)
		bmp->bits[i] = 0;
}

static __always_inline u64 bmp_last_word_mask(size_t bits)
{
	u32 rem = bits % BITS_PER_LONG_LONG;

	return rem ? (1ULL << rem) - 1 : ~0ULL;
}

__weak
void bmp_and(size_t bits, struct arena_bitmap __arena *dst, struct arena_bitmap __arena *src1, struct arena_bitmap __arena *src2)
{
	size_t nwords = BITS_TO_LONG_LONGS(bits);
	volatile u32 i;

	for (i = zero; i < nwords && can_loop; i++)
		dst->bits[i] = src1->bits[i] & src2->bits[i];

	if (nwords && bits % BITS_PER_LONG_LONG)
		dst->bits[nwords - 1] &= bmp_last_word_mask(bits);
}

__weak
void bmp_or(size_t bits, struct arena_bitmap __arena *dst, struct arena_bitmap __arena *src1, struct arena_bitmap __arena *src2)
{
	size_t nwords = BITS_TO_LONG_LONGS(bits);
	volatile u32 i;

	for (i = zero; i < nwords && can_loop; i++)
		dst->bits[i] = src1->bits[i] | src2->bits[i];

	if (nwords && bits % BITS_PER_LONG_LONG)
		dst->bits[nwords - 1] &= bmp_last_word_mask(bits);
}

__weak
bool bmp_empty(size_t bits, struct arena_bitmap __arena *bmp)
{
	size_t nwords = BITS_TO_LONG_LONGS(bits);
	volatile u32 i;

	for (i = zero; i < nwords && can_loop; i++) {
		u64 mask = (i == nwords - 1) ? bmp_last_word_mask(bits) : ~0ULL;

		if (bmp->bits[i] & mask)
			return false;
	}

	return true;
}

__weak
void bmp_copy(size_t bits, struct arena_bitmap __arena *dst, struct arena_bitmap __arena *src)
{
	size_t nwords = BITS_TO_LONG_LONGS(bits);
	volatile u32 i;

	for (i = zero; i < nwords && can_loop; i++)
		dst->bits[i] = src->bits[i];

	if (nwords && bits % BITS_PER_LONG_LONG)
		dst->bits[nwords - 1] &= bmp_last_word_mask(bits);
}

__weak
bool bmp_subset(size_t bits, struct arena_bitmap __arena *big, struct arena_bitmap __arena *small)
{
	size_t nwords = BITS_TO_LONG_LONGS(bits);
	volatile u32 i;

	for (i = zero; i < nwords && can_loop; i++) {
		u64 mask = (i == nwords - 1) ? bmp_last_word_mask(bits) : ~0ULL;

		if (~big->bits[i] & small->bits[i] & mask)
			return false;
	}

	return true;
}

__weak
bool bmp_intersects(size_t bits, struct arena_bitmap __arena *arg1, struct arena_bitmap __arena *arg2)
{
	size_t nwords = BITS_TO_LONG_LONGS(bits);
	volatile u32 i;

	for (i = zero; i < nwords && can_loop; i++) {
		u64 mask = (i == nwords - 1) ? bmp_last_word_mask(bits) : ~0ULL;

		if (arg1->bits[i] & arg2->bits[i] & mask)
			return true;
	}

	return false;
}

__weak
void bmp_print(size_t bits, struct arena_bitmap __arena *bmp)
{
	size_t nwords = BITS_TO_LONG_LONGS(bits);
	volatile u32 i;

	for (i = zero; i < nwords && can_loop; i++)
		arena_stderr("%016llx ", bmp->bits[i]);
}
