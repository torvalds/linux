// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include "bpf_experimental.h"

struct arr_elem {
	struct bpf_res_spin_lock lock;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct arr_elem);
} arrmap SEC(".maps");

#define ENOSPC 28
#define _STR "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"

int size;

SEC("syscall")
__success __retval(0)
int stream_exhaust(void *ctx)
{
	/* Use global variable for loop convergence. */
	size = 0;
	bpf_repeat(BPF_MAX_LOOPS) {
		if (bpf_stream_printk(BPF_STDOUT, _STR) == -ENOSPC && size == 99954)
			return 0;
		size += sizeof(_STR) - 1;
	}
	return 1;
}

SEC("syscall")
__success __retval(0)
int stream_cond_break(void *ctx)
{
	while (can_loop)
		;
	return 0;
}

SEC("syscall")
__success __retval(0)
int stream_deadlock(void *ctx)
{
	struct bpf_res_spin_lock *lock, *nlock;

	lock = bpf_map_lookup_elem(&arrmap, &(int){0});
	if (!lock)
		return 1;
	nlock = bpf_map_lookup_elem(&arrmap, &(int){0});
	if (!nlock)
		return 1;
	if (bpf_res_spin_lock(lock))
		return 1;
	if (bpf_res_spin_lock(nlock)) {
		bpf_res_spin_unlock(lock);
		return 0;
	}
	bpf_res_spin_unlock(nlock);
	bpf_res_spin_unlock(lock);
	return 1;
}

SEC("syscall")
__success __retval(0)
int stream_syscall(void *ctx)
{
	bpf_stream_printk(BPF_STDOUT, "foo");
	return 0;
}

char _license[] SEC("license") = "GPL";
