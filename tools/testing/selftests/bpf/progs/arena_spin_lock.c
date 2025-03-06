// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include "bpf_arena_spin_lock.h"

struct {
	__uint(type, BPF_MAP_TYPE_ARENA);
	__uint(map_flags, BPF_F_MMAPABLE);
	__uint(max_entries, 100); /* number of pages */
#ifdef __TARGET_ARCH_arm64
	__ulong(map_extra, 0x1ull << 32); /* start of mmap() region */
#else
	__ulong(map_extra, 0x1ull << 44); /* start of mmap() region */
#endif
} arena SEC(".maps");

int cs_count;

#if defined(ENABLE_ATOMICS_TESTS) && defined(__BPF_FEATURE_ADDR_SPACE_CAST)
arena_spinlock_t __arena lock;
int test_skip = 1;
#else
int test_skip = 2;
#endif

int counter;
int limit;

SEC("tc")
int prog(void *ctx)
{
	int ret = -2;

#if defined(ENABLE_ATOMICS_TESTS) && defined(__BPF_FEATURE_ADDR_SPACE_CAST)
	unsigned long flags;

	if ((ret = arena_spin_lock_irqsave(&lock, flags)))
		return ret;
	if (counter != limit)
		counter++;
	bpf_repeat(cs_count);
	ret = 0;
	arena_spin_unlock_irqrestore(&lock, flags);
#endif
	return ret;
}

char _license[] SEC("license") = "GPL";
