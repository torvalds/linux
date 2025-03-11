// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>

#include "bpf_misc.h"
#include "bpf_experimental.h"

extern void bpf_rcu_read_lock(void) __ksym;
extern void bpf_rcu_read_unlock(void) __ksym;

#define private(name) SEC(".bss." #name) __hidden __attribute__((aligned(8)))

private(A) struct bpf_spin_lock lock;

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 3);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u32));
} jmp_table SEC(".maps");

SEC("?tc")
__failure __msg("function calls are not allowed while holding a lock")
int reject_tail_call_spin_lock(struct __sk_buff *ctx)
{
	bpf_spin_lock(&lock);
	bpf_tail_call_static(ctx, &jmp_table, 0);
	return 0;
}

SEC("?tc")
__failure __msg("tail_call cannot be used inside bpf_rcu_read_lock-ed region")
int reject_tail_call_rcu_lock(struct __sk_buff *ctx)
{
	bpf_rcu_read_lock();
	bpf_tail_call_static(ctx, &jmp_table, 0);
	bpf_rcu_read_unlock();
	return 0;
}

SEC("?tc")
__failure __msg("tail_call cannot be used inside bpf_preempt_disable-ed region")
int reject_tail_call_preempt_lock(struct __sk_buff *ctx)
{
	bpf_guard_preempt();
	bpf_tail_call_static(ctx, &jmp_table, 0);
	return 0;
}

SEC("?tc")
__failure __msg("tail_call would lead to reference leak")
int reject_tail_call_ref(struct __sk_buff *ctx)
{
	struct foo { int i; } *p;

	p = bpf_obj_new(typeof(*p));
	bpf_tail_call_static(ctx, &jmp_table, 0);
	return 0;
}

char _license[] SEC("license") = "GPL";
