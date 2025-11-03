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

__weak int subprog_untrusted_bad_tags(struct task_struct *task __arg_untrusted __arg_nullable)
{
	return task->pid;
}

SEC("tp_btf/sys_enter")
__failure
__msg("arg#0 untrusted cannot be combined with any other tags")
int untrusted_bad_tags(void *ctx)
{
	return subprog_untrusted_bad_tags(0);
}

struct local_type_wont_be_accepted {};

__weak int subprog_untrusted_bad_type(struct local_type_wont_be_accepted *p __arg_untrusted)
{
	return 0;
}

SEC("tp_btf/sys_enter")
__failure
__msg("arg#0 reference type('STRUCT local_type_wont_be_accepted') has no matches")
int untrusted_bad_type(void *ctx)
{
	return subprog_untrusted_bad_type(bpf_rdonly_cast(0, 0));
}

__weak int subprog_untrusted(const volatile struct task_struct *restrict task __arg_untrusted)
{
	return task->pid;
}

SEC("tp_btf/sys_enter")
__success
__log_level(2)
__msg("r1 = {{.*}}; {{.*}}R1=trusted_ptr_task_struct()")
__msg("Func#1 ('subprog_untrusted') is global and assumed valid.")
__msg("Validating subprog_untrusted() func#1...")
__msg(": R1=untrusted_ptr_task_struct")
int trusted_to_untrusted(void *ctx)
{
	return subprog_untrusted(bpf_get_current_task_btf());
}

char mem[16];
u32 offset;

SEC("tp_btf/sys_enter")
__success
int anything_to_untrusted(void *ctx)
{
	/* untrusted to untrusted */
	subprog_untrusted(bpf_core_cast(0, struct task_struct));
	/* wrong type to untrusted */
	subprog_untrusted((void *)bpf_core_cast(0, struct bpf_verifier_env));
	/* map value to untrusted */
	subprog_untrusted((void *)mem);
	/* scalar to untrusted */
	subprog_untrusted(0);
	/* variable offset to untrusted (map) */
	subprog_untrusted((void *)mem + offset);
	/* variable offset to untrusted (trusted) */
	subprog_untrusted((void *)bpf_get_current_task_btf() + offset);
	return 0;
}

__weak int subprog_untrusted2(struct task_struct *task __arg_untrusted)
{
	return subprog_trusted_task_nullable(task);
}

SEC("tp_btf/sys_enter")
__failure
__msg("R1 type=untrusted_ptr_ expected=ptr_, trusted_ptr_, rcu_ptr_")
__msg("Caller passes invalid args into func#{{.*}} ('subprog_trusted_task_nullable')")
int untrusted_to_trusted(void *ctx)
{
	return subprog_untrusted2(bpf_get_current_task_btf());
}

__weak int subprog_void_untrusted(void *p __arg_untrusted)
{
	return *(int *)p;
}

__weak int subprog_char_untrusted(char *p __arg_untrusted)
{
	return *(int *)p;
}

__weak int subprog_enum_untrusted(enum bpf_attach_type *p __arg_untrusted)
{
	return *(int *)p;
}

SEC("tp_btf/sys_enter")
__success
__log_level(2)
__msg("r1 = {{.*}}; {{.*}}R1=trusted_ptr_task_struct()")
__msg("Func#1 ('subprog_void_untrusted') is global and assumed valid.")
__msg("Validating subprog_void_untrusted() func#1...")
__msg(": R1=rdonly_untrusted_mem(sz=0)")
int trusted_to_untrusted_mem(void *ctx)
{
	return subprog_void_untrusted(bpf_get_current_task_btf());
}

SEC("tp_btf/sys_enter")
__success
int anything_to_untrusted_mem(void *ctx)
{
	/* untrusted to untrusted mem */
	subprog_void_untrusted(bpf_core_cast(0, struct task_struct));
	/* map value to untrusted mem */
	subprog_void_untrusted(mem);
	/* scalar to untrusted mem */
	subprog_void_untrusted(0);
	/* variable offset to untrusted mem (map) */
	subprog_void_untrusted((void *)mem + offset);
	/* variable offset to untrusted mem (trusted) */
	subprog_void_untrusted(bpf_get_current_task_btf() + offset);
	/* variable offset to untrusted char/enum (map) */
	subprog_char_untrusted(mem + offset);
	subprog_enum_untrusted((void *)mem + offset);
	return 0;
}

char _license[] SEC("license") = "GPL";
