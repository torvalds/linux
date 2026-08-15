// SPDX-License-Identifier: LGPL-2.1 OR BSD-2-Clause
/*
 * Copyright (c) 2025-2026 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2025-2026 Emil Tsalapatis <etsal@meta.com>
 */

#include <bpf_atomic.h>

#include <libarena/common.h>

#include <libarena/asan.h>
#include <libarena/spmc.h>

static inline
u64 spmc_arr_size(volatile struct spmc_arr __arena *spmc_arr)
{
	return SPMC_ARR_BASESZ << spmc_arr->order;
}

static inline
u64 spmc_arr_get(volatile struct spmc_arr __arena *spmc_arr, u64 ind)
{
	u64 ret = READ_ONCE(spmc_arr->data[ind % spmc_arr_size(spmc_arr)]);

	return ret;
}

static inline
void spmc_arr_put(volatile struct spmc_arr __arena *spmc_arr, u64 ind, u64 value)
{
	WRITE_ONCE(spmc_arr->data[ind % spmc_arr_size(spmc_arr)], value);
}

static inline
void spmc_arr_copy(volatile struct spmc_arr __arena *dst,
		   volatile struct spmc_arr __arena *src, u64 b, u64 t)
{
	u64 i;

	for (i = t; i < b && can_loop; i++)
		spmc_arr_put(dst, i, spmc_arr_get(src, i));
}

static inline
int spmc_order_init(struct spmc __arena *spmc, int order)
{
	volatile struct spmc_arr __arena *arr = &spmc->arr[order];

	if (unlikely(!spmc))
		return -EINVAL;

	if (order >= SPMC_ARR_ORDERS)
		return -E2BIG;

	/* Already allocated? */
	if (arr->data)
		return 0;

	arr->data = arena_malloc((SPMC_ARR_BASESZ << order) * sizeof(*arr->data));
	if (!arr->data)
		return -ENOMEM;

	return 0;
}

__weak
int spmc_owned_add(struct spmc __arena *spmc, u64 val)
{
	volatile struct spmc_arr __arena *newarr;
	volatile struct spmc_arr __arena *arr;
	ssize_t sz;
	u64 b, t;
	int ret;

	if (unlikely(!spmc))
		return -EINVAL;

	/* 
	 * Bottom must always be read first, also
	 * see spmc_steal().
	 */
	b = smp_load_acquire(&spmc->bottom);
	t = READ_ONCE(spmc->top);
	arr = READ_ONCE(spmc->cur);

	sz = b - t;
	if (sz >= spmc_arr_size(arr) - 1) {
		ret = spmc_order_init(spmc, arr->order + 1);
		if (ret)
			return ret;

		newarr = &spmc->arr[arr->order + 1];

		spmc_arr_copy(newarr, arr, b, t);
		smp_store_release(&spmc->cur, newarr);
		arr = newarr;
	}

	spmc_arr_put(arr, b, val);
	smp_store_release(&spmc->bottom, b + 1);

	return 0;
}


__weak
int spmc_owned_remove(struct spmc __arena *spmc, u64 *val)
{
	volatile struct spmc_arr __arena *arr;
	int ret = 0;
	ssize_t sz;
	u64 value;
	u64 b, t;

	if (unlikely(!spmc || !val))
		return -EINVAL;

	b = READ_ONCE(spmc->bottom) - 1;
	WRITE_ONCE(spmc->bottom, b);
	smp_mb();

	t = READ_ONCE(spmc->top);
	arr = READ_ONCE(spmc->cur);

	sz = b - t;
	if (sz < 0) {
		WRITE_ONCE(spmc->bottom, t);
		return -ENOENT;
	}

	value = spmc_arr_get(arr, b);
	if (sz > 0) {
		*val = value;
		return 0;
	}

	if (cmpxchg(&spmc->top, t, t + 1) != t)
		ret = -EAGAIN;

	WRITE_ONCE(spmc->bottom, t + 1);

	if (ret)
		return ret;

	*val = value;

	return 0;
}

__weak
int spmc_steal(struct spmc __arena *spmc, u64 *val)
{
	volatile struct spmc_arr __arena *arr;
	ssize_t sz;
	u64 value;
	u64 b, t;

	if (unlikely(!spmc || !val))
		return -EINVAL;

	/*
	 * It is important that t is read before b for
	 * stealers to avoid racing with the owner.
	 * Races between stealers are dealt with using
	 * CAS to increment the top value below.
	 */
	t = smp_load_acquire(&spmc->top);
	b = smp_load_acquire(&spmc->bottom);

	sz = b - t;
	if (sz <= 0)
		return -ENOENT;

	arr = smp_load_acquire(&spmc->cur);
	value = spmc_arr_get(arr, t);

	if (cmpxchg(&spmc->top, t, t + 1) != t)
		return -EAGAIN;

	*val = value;

	return 0;
}


__weak
struct spmc __arena *spmc_create(void)
{
	/*
	 * Marked as volatile because otherwise the array
	 * reference in the internal loop gets demoted to
	 * scalar and the program fails verification.
	 */
	struct spmc __arena *volatile spmc;
	int ret, i;

	spmc = arena_malloc(sizeof(*spmc));
	if (!spmc)
		return NULL;

	spmc->bottom = 0;
	spmc->top = 0;

	for (i = 0; i < SPMC_ARR_ORDERS && can_loop; i++) {
		spmc->arr[i].data = NULL;
		spmc->arr[i].order = i;
	}

	ret = spmc_order_init((struct spmc __arena *)spmc, 0);
	if (ret) {
		arena_free(spmc);
		return NULL;
	}

	spmc->cur = &spmc->arr[0];

	return (struct spmc __arena *)spmc;
}

__weak
int spmc_destroy(struct spmc __arena *spmc)
{
	int i;

	if (unlikely(!spmc))
		return -EINVAL;

	for (i = 0; i < SPMC_ARR_ORDERS && can_loop; i++)
		arena_free(spmc->arr[i].data);

	arena_free(spmc);

	return 0;
}
