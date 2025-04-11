// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"
#include "bpf_experimental.h"

extern int bpf_copy_from_user_str(void *dst, u32 dst__sz, const void *unsafe_ptr__ign, u64 flags) __weak __ksym;

SEC("?tc")
__failure __msg("BPF_EXIT instruction in main prog cannot be used inside bpf_preempt_disable-ed region")
int preempt_lock_missing_1(struct __sk_buff *ctx)
{
	bpf_preempt_disable();
	return 0;
}

SEC("?tc")
__failure __msg("BPF_EXIT instruction in main prog cannot be used inside bpf_preempt_disable-ed region")
int preempt_lock_missing_2(struct __sk_buff *ctx)
{
	bpf_preempt_disable();
	bpf_preempt_disable();
	return 0;
}

SEC("?tc")
__failure __msg("BPF_EXIT instruction in main prog cannot be used inside bpf_preempt_disable-ed region")
int preempt_lock_missing_3(struct __sk_buff *ctx)
{
	bpf_preempt_disable();
	bpf_preempt_disable();
	bpf_preempt_disable();
	return 0;
}

SEC("?tc")
__failure __msg("BPF_EXIT instruction in main prog cannot be used inside bpf_preempt_disable-ed region")
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
__failure __msg("BPF_EXIT instruction in main prog cannot be used inside bpf_preempt_disable-ed region")
int preempt_lock_missing_1_subprog(struct __sk_buff *ctx)
{
	preempt_disable();
	return 0;
}

SEC("?tc")
__failure __msg("BPF_EXIT instruction in main prog cannot be used inside bpf_preempt_disable-ed region")
int preempt_lock_missing_2_subprog(struct __sk_buff *ctx)
{
	preempt_disable();
	preempt_disable();
	return 0;
}

SEC("?tc")
__failure __msg("BPF_EXIT instruction in main prog cannot be used inside bpf_preempt_disable-ed region")
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

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
__failure __msg("kernel func bpf_copy_from_user_str is sleepable within non-preemptible region")
int preempt_sleepable_kfunc(void *ctx)
{
	u32 data;

	bpf_preempt_disable();
	bpf_copy_from_user_str(&data, sizeof(data), NULL, 0);
	bpf_preempt_enable();
	return 0;
}

int __noinline preempt_global_subprog(void)
{
	preempt_balance_subprog();
	return 0;
}

SEC("?tc")
__success
int preempt_global_subprog_test(struct __sk_buff *ctx)
{
	preempt_disable();
	preempt_global_subprog();
	preempt_enable();
	return 0;
}

int __noinline
global_subprog(int i)
{
	if (i)
		bpf_printk("%p", &i);
	return i;
}

int __noinline
global_sleepable_helper_subprog(int i)
{
	if (i)
		bpf_copy_from_user(&i, sizeof(i), NULL);
	return i;
}

int __noinline
global_sleepable_kfunc_subprog(int i)
{
	if (i)
		bpf_copy_from_user_str(&i, sizeof(i), NULL, 0);
	global_subprog(i);
	return i;
}

int __noinline
global_subprog_calling_sleepable_global(int i)
{
	if (!i)
		global_sleepable_kfunc_subprog(i);
	return i;
}

SEC("?syscall")
__failure __msg("global functions that may sleep are not allowed in non-sleepable context")
int preempt_global_sleepable_helper_subprog(struct __sk_buff *ctx)
{
	preempt_disable();
	if (ctx->mark)
		global_sleepable_helper_subprog(ctx->mark);
	preempt_enable();
	return 0;
}

SEC("?syscall")
__failure __msg("global functions that may sleep are not allowed in non-sleepable context")
int preempt_global_sleepable_kfunc_subprog(struct __sk_buff *ctx)
{
	preempt_disable();
	if (ctx->mark)
		global_sleepable_kfunc_subprog(ctx->mark);
	preempt_enable();
	return 0;
}

SEC("?syscall")
__failure __msg("global functions that may sleep are not allowed in non-sleepable context")
int preempt_global_sleepable_subprog_indirect(struct __sk_buff *ctx)
{
	preempt_disable();
	if (ctx->mark)
		global_subprog_calling_sleepable_global(ctx->mark);
	preempt_enable();
	return 0;
}

char _license[] SEC("license") = "GPL";
