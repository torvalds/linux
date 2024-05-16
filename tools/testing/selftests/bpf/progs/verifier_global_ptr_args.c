// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "bpf_misc.h"
#include "xdp_metadata.h"
#include "bpf_kfuncs.h"

extern struct task_struct *bpf_task_acquire(struct task_struct *p) __ksym __weak;
extern void bpf_task_release(struct task_struct *p) __ksym __weak;

__weak int subprog_trusted_task_nullable(struct task_struct *task __arg_trusted __arg_nullable)
{
	if (!task)
		return 0;
	return task->pid + task->tgid;
}

__weak int subprog_trusted_task_nullable_extra_layer(struct task_struct *task __arg_trusted __arg_nullable)
{
	return subprog_trusted_task_nullable(task) + subprog_trusted_task_nullable(NULL);
}

SEC("?tp_btf/task_newtask")
__success __log_level(2)
__msg("Validating subprog_trusted_task_nullable() func#1...")
__msg(": R1=trusted_ptr_or_null_task_struct(")
int trusted_task_arg_nullable(void *ctx)
{
	struct task_struct *t1 = bpf_get_current_task_btf();
	struct task_struct *t2 = bpf_task_acquire(t1);
	int res = 0;

	/* known NULL */
	res += subprog_trusted_task_nullable(NULL);

	/* known non-NULL */
	res += subprog_trusted_task_nullable(t1);
	res += subprog_trusted_task_nullable_extra_layer(t1);

	/* unknown if NULL or not */
	res += subprog_trusted_task_nullable(t2);
	res += subprog_trusted_task_nullable_extra_layer(t2);

	if (t2) {
		/* known non-NULL after explicit NULL check, just in case */
		res += subprog_trusted_task_nullable(t2);
		res += subprog_trusted_task_nullable_extra_layer(t2);

		bpf_task_release(t2);
	}

	return res;
}

__weak int subprog_trusted_task_nonnull(struct task_struct *task __arg_trusted)
{
	return task->pid + task->tgid;
}

SEC("?kprobe")
__failure __log_level(2)
__msg("R1 type=scalar expected=ptr_, trusted_ptr_, rcu_ptr_")
__msg("Caller passes invalid args into func#1 ('subprog_trusted_task_nonnull')")
int trusted_task_arg_nonnull_fail1(void *ctx)
{
	return subprog_trusted_task_nonnull(NULL);
}

SEC("?tp_btf/task_newtask")
__failure __log_level(2)
__msg("R1 type=ptr_or_null_ expected=ptr_, trusted_ptr_, rcu_ptr_")
__msg("Caller passes invalid args into func#1 ('subprog_trusted_task_nonnull')")
int trusted_task_arg_nonnull_fail2(void *ctx)
{
	struct task_struct *t = bpf_get_current_task_btf();
	struct task_struct *nullable;
	int res;

	nullable = bpf_task_acquire(t);

	 /* should fail, PTR_TO_BTF_ID_OR_NULL */
	res = subprog_trusted_task_nonnull(nullable);

	if (nullable)
		bpf_task_release(nullable);

	return res;
}

SEC("?kprobe")
__success __log_level(2)
__msg("Validating subprog_trusted_task_nonnull() func#1...")
__msg(": R1=trusted_ptr_task_struct(")
int trusted_task_arg_nonnull(void *ctx)
{
	struct task_struct *t = bpf_get_current_task_btf();

	return subprog_trusted_task_nonnull(t);
}

struct task_struct___local {} __attribute__((preserve_access_index));

__weak int subprog_nullable_task_flavor(
	struct task_struct___local *task __arg_trusted __arg_nullable)
{
	char buf[16];

	if (!task)
		return 0;

	return bpf_copy_from_user_task(&buf, sizeof(buf), NULL, (void *)task, 0);
}

SEC("?uprobe.s")
__success __log_level(2)
__msg("Validating subprog_nullable_task_flavor() func#1...")
__msg(": R1=trusted_ptr_or_null_task_struct(")
int flavor_ptr_nullable(void *ctx)
{
	struct task_struct___local *t = (void *)bpf_get_current_task_btf();

	return subprog_nullable_task_flavor(t);
}

__weak int subprog_nonnull_task_flavor(struct task_struct___local *task __arg_trusted)
{
	char buf[16];

	return bpf_copy_from_user_task(&buf, sizeof(buf), NULL, (void *)task, 0);
}

SEC("?uprobe.s")
__success __log_level(2)
__msg("Validating subprog_nonnull_task_flavor() func#1...")
__msg(": R1=trusted_ptr_task_struct(")
int flavor_ptr_nonnull(void *ctx)
{
	struct task_struct *t = bpf_get_current_task_btf();

	return subprog_nonnull_task_flavor((void *)t);
}

__weak int subprog_trusted_destroy(struct task_struct *task __arg_trusted)
{
	bpf_task_release(task); /* should be rejected */

	return 0;
}

SEC("?tp_btf/task_newtask")
__failure __log_level(2)
__msg("release kernel function bpf_task_release expects refcounted PTR_TO_BTF_ID")
int BPF_PROG(trusted_destroy_fail, struct task_struct *task, u64 clone_flags)
{
	return subprog_trusted_destroy(task);
}

__weak int subprog_trusted_acq_rel(struct task_struct *task __arg_trusted)
{
	struct task_struct *owned;

	owned = bpf_task_acquire(task);
	if (!owned)
		return 0;

	bpf_task_release(owned); /* this one is OK, we acquired it locally */

	return 0;
}

SEC("?tp_btf/task_newtask")
__success __log_level(2)
int BPF_PROG(trusted_acq_rel, struct task_struct *task, u64 clone_flags)
{
	return subprog_trusted_acq_rel(task);
}

char _license[] SEC("license") = "GPL";
