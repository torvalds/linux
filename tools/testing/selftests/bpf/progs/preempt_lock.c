// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"
#include "bpf_experimental.h"

SEC("?tc")
__failure __msg("1 bpf_preempt_enable is missing")
int preempt_lock_missing_1(struct __sk_buff *ctx)
{
	bpf_preempt_disable();
	return 0;
}

SEC("?tc")
__failure __msg("2 bpf_preempt_enable(s) are missing")
int preempt_lock_missing_2(struct __sk_buff *ctx)
{
	bpf_preempt_disable();
	bpf_preempt_disable();
	return 0;
}

SEC("?tc")
__failure __msg("3 bpf_preempt_enable(s) are missing")
int preempt_lock_missing_3(struct __sk_buff *ctx)
{
	bpf_preempt_disable();
	bpf_preempt_disable();
	bpf_preempt_disable();
	return 0;
}

SEC("?tc")
__failure __msg("1 bpf_preempt_enable is missing")
int preempt_lock_missing_3_minus_2(struct __sk_buff *ctx)
{
	bpf_preempt_disable();
	bpf_preempt_disable();
	bpf_preempt_disable();
	bpf_preempt_enable();
	bpf_preempt_enable();
	return 0;
}

static __noinline void preempt_disable(void)
{
	bpf_preempt_disable();
}

static __noinline void preempt_enable(void)
{
	bpf_preempt_enable();
}

SEC("?tc")
__failure __msg("1 bpf_preempt_enable is missing")
int preempt_lock_missing_1_subprog(struct __sk_buff *ctx)
{
	preempt_disable();
	return 0;
}

SEC("?tc")
__failure __msg("2 bpf_preempt_enable(s) are missing")
int preempt_lock_missing_2_subprog(struct __sk_buff *ctx)
{
	preempt_disable();
	preempt_disable();
	return 0;
}

SEC("?tc")
__failure __msg("1 bpf_preempt_enable is missing")
int preempt_lock_missing_2_minus_1_subprog(struct __sk_buff *ctx)
{
	preempt_disable();
	preempt_disable();
	preempt_enable();
	return 0;
}

static __noinline void preempt_balance_subprog(void)
{
	preempt_disable();
	preempt_enable();
}

SEC("?tc")
__success int preempt_balance(struct __sk_buff *ctx)
{
	bpf_guard_preempt();
	return 0;
}

SEC("?tc")
__success int preempt_balance_subprog_test(struct __sk_buff *ctx)
{
	preempt_balance_subprog();
	return 0;
}

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
__failure __msg("sleepable helper bpf_copy_from_user#")
int preempt_sleepable_helper(void *ctx)
{
	u32 data;

	bpf_preempt_disable();
	bpf_copy_from_user(&data, sizeof(data), NULL);
	bpf_preempt_enable();
	return 0;
}

int __noinline preempt_global_subprog(void)
{
	preempt_balance_subprog();
	return 0;
}

SEC("?tc")
__failure __msg("global function calls are not allowed with preemption disabled")
int preempt_global_subprog_test(struct __sk_buff *ctx)
{
	preempt_disable();
	preempt_global_subprog();
	preempt_enable();
	return 0;
}

char _license[] SEC("license") = "GPL";
