// SPDX-License-Identifier: LGPL-2.1 OR BSD-2-Clause
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#pragma once

#define ST_PAGES 64

static inline void print_asan_map_state(void __arena *addr)
{
	arena_stdout("%s:%d ASAN %p -> (val: %x gran: %x set: [%s])",
			__func__, __LINE__, addr,
			*(s8 __arena *)(addr), ASAN_GRANULE(addr),
			asan_shadow_set(addr) ? "yes" : "no");
}

/*
 * Emit an error and force the current function to exit if the ASAN
 * violation state is unexpected. Reset the violation state after.
 */
static inline int asan_validate_addr(bool cond, void __arena *addr)
{
	if ((asan_violated != 0) == cond) {
		asan_violated = 0;
		return 0;
	}

	arena_stdout("%s:%d ASAN asan_violated %lx", __func__, __LINE__,
			(u64)asan_violated);
	print_asan_map_state(addr);

	asan_violated = 0;

	return -EINVAL;
}

static inline int asan_validate(void)
{
	if (!asan_violated)
		return 0;

	arena_stdout("%s:%d Found ASAN violation at %lx", __func__, __LINE__,
			asan_violated);

	asan_violated = 0;

	return -EINVAL;
}

struct blob {
	volatile u8 mem[59];
	u8 oob;
};
