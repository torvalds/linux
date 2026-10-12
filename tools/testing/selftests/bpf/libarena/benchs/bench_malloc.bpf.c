// SPDX-License-Identifier: LGPL-2.1 OR BSD-2-Clause
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <libarena/common.h>

#include <libarena/asan.h>
#include <libarena/buddy.h>

u32 bench_alloc_size;
u32 bench_nallocs;
long bench_hits;
long bench_duration_ns;

SEC("syscall")
int bench_malloc(void)
{
	void __arena *mem;
	u64 start_ns;
	u32 i;

	start_ns = bpf_ktime_get_ns();
	for (i = zero; i < bench_nallocs && can_loop; i++) {
		mem = arena_malloc(bench_alloc_size);
		if (!mem)
			return -ENOMEM;
	}

	__sync_add_and_fetch(&bench_duration_ns,
			     bpf_ktime_get_ns() - start_ns);
	__sync_add_and_fetch(&bench_hits, i);
	return 0;
}

SEC("syscall")
int bench_calloc(void)
{
	void __arena *mem;
	u64 start_ns;
	u32 i;

	start_ns = bpf_ktime_get_ns();
	for (i = zero; i < bench_nallocs && can_loop; i++) {
		mem = arena_calloc(1, bench_alloc_size);
		if (!mem)
			return -ENOMEM;
	}

	__sync_add_and_fetch(&bench_duration_ns,
			     bpf_ktime_get_ns() - start_ns);
	__sync_add_and_fetch(&bench_hits, i);
	return 0;
}
