// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>

#include "../bpf_experimental.h"
#include "task_kfunc_common.h"

char _license[] SEC("license") = "GPL";

int err, pid;

/* Prototype for all of the program trace events below:
 *
 * TRACE_EVENT(task_newtask,
 *         TP_PROTO(struct task_struct *p, u64 clone_flags)
 */

struct task_struct *bpf_task_acquire(struct task_struct *p) __ksym __weak;

struct task_struct *bpf_task_acquire___one(struct task_struct *task) __ksym __weak;
/* The two-param bpf_task_acquire doesn't exist */
struct task_struct *bpf_task_acquire___two(struct task_struct *p, void *ctx) __ksym __weak;
/* Incorrect type for first param */
struct task_struct *bpf_task_acquire___three(void *ctx) __ksym __weak;

void invalid_kfunc(void) __ksym __weak;
void bpf_testmod_test_mod_kfunc(int i) __ksym __weak;

static bool is_test_kfunc_task(void)
{
	int cur_pid = bpf_get_current_pid_tgid() >> 32;

	return pid == cur_pid;
}

static int test_acquire_release(struct task_struct *task)
{
	struct task_struct *acquired = NULL;

	if (!bpf_ksym_exists(bpf_task_acquire)) {
		err = 3;
		return 0;
	}
	if (!bpf_ksym_exists(bpf_testmod_test_mod_kfunc)) {
		err = 4;
		return 0;
	}
	if (bpf_ksym_exists(invalid_kfunc)) {
		/* the verifier's dead code elimination should remove this */
		err = 5;
		asm volatile ("goto -1"); /* for (;;); */
	}

	acquired = bpf_task_acquire(task);
	if (acquired)
		bpf_task_release(acquired);
	else
		err = 6;

	return 0;
}

SEC("tp_btf/task_newtask")
int BPF_PROG(test_task_kfunc_flavor_relo, struct task_struct *task, u64 clone_flags)
{
	struct task_struct *acquired = NULL;
	int fake_ctx = 42;

	if (bpf_ksym_exists(bpf_task_acquire___one)) {
		acquired = bpf_task_acquire___one(task);
	} else if (bpf_ksym_exists(bpf_task_acquire___two)) {
		/* Here, bpf_object__resolve_ksym_func_btf_id's find_ksym_btf_id
		 * call will find vmlinux's bpf_task_acquire, but subsequent
		 * bpf_core_types_are_compat will fail
		 */
		acquired = bpf_task_acquire___two(task, &fake_ctx);
		err = 3;
		return 0;
	} else if (bpf_ksym_exists(bpf_task_acquire___three)) {
		/* bpf_core_types_are_compat will fail similarly to above case */
		acquired = bpf_task_acquire___three(&fake_ctx);
		err = 4;
		return 0;
	}

	if (acquired)
		bpf_task_release(acquired);
	else
		err = 5;
	return 0;
}

SEC("tp_btf/task_newtask")
int BPF_PROG(test_task_kfunc_flavor_relo_not_found, struct task_struct *task, u64 clone_flags)
{
	/* Neither symbol should successfully resolve.
	 * Success or failure of one ___flavor should not affect others
	 */
	if (bpf_ksym_exists(bpf_task_acquire___two))
		err = 1;
	else if (bpf_ksym_exists(bpf_task_acquire___three))
		err = 2;

	return 0;
}

SEC("tp_btf/task_newtask")
int BPF_PROG(test_task_acquire_release_argument, struct task_struct *task, u64 clone_flags)
{
	if (!is_test_kfunc_task())
		return 0;

	return test_acquire_release(task);
}

SEC("tp_btf/task_newtask")
int BPF_PROG(test_task_acquire_release_current, struct task_struct *task, u64 clone_flags)
{
	if (!is_test_kfunc_task())
		return 0;

	return test_acquire_release(bpf_get_current_task_btf());
}

SEC("tp_btf/task_newtask")
int BPF_PROG(test_task_acquire_leave_in_map, struct task_struct *task, u64 clone_flags)
{
	long status;

	if (!is_test_kfunc_task())
		return 0;

	status = tasks_kfunc_map_insert(task);
	if (status)
		err = 1;

	return 0;
}

SEC("tp_btf/task_newtask")
int BPF_PROG(test_task_xchg_release, struct task_struct *task, u64 clone_flags)
{
	struct task_struct *kptr, *acquired;
	struct __tasks_kfunc_map_value *v, *local;
	int refcnt, refcnt_after_drop;
	long status;

	if (!is_test_kfunc_task())
		return 0;

	status = tasks_kfunc_map_insert(task);
	if (status) {
		err = 1;
		return 0;
	}

	v = tasks_kfunc_map_value_lookup(task);
	if (!v) {
		err = 2;
		return 0;
	}

	kptr = bpf_kptr_xchg(&v->task, NULL);
	if (!kptr) {
		err = 3;
		return 0;
	}

	local = bpf_obj_new(typeof(*local));
	if (!local) {
		err = 4;
		bpf_task_release(kptr);
		return 0;
	}

	kptr = bpf_kptr_xchg(&local->task, kptr);
	if (kptr) {
		err = 5;
		bpf_obj_drop(local);
		bpf_task_release(kptr);
		return 0;
	}

	kptr = bpf_kptr_xchg(&local->task, NULL);
	if (!kptr) {
		err = 6;
		bpf_obj_drop(local);
		return 0;
	}

	/* Stash a copy into local kptr and check if it is released recursively */
	acquired = bpf_task_acquire(kptr);
	if (!acquired) {
		err = 7;
		bpf_obj_drop(local);
		bpf_task_release(kptr);
		return 0;
	}
	bpf_probe_read_kernel(&refcnt, sizeof(refcnt), &acquired->rcu_users);

	acquired = bpf_kptr_xchg(&local->task, acquired);
	if (acquired) {
		err = 8;
		bpf_obj_drop(local);
		bpf_task_release(kptr);
		bpf_task_release(acquired);
		return 0;
	}

	bpf_obj_drop(local);

	bpf_probe_read_kernel(&refcnt_after_drop, sizeof(refcnt_after_drop), &kptr->rcu_users);
	if (refcnt != refcnt_after_drop + 1) {
		err = 9;
		bpf_task_release(kptr);
		return 0;
	}

	bpf_task_release(kptr);

	return 0;
}

SEC("tp_btf/task_newtask")
int BPF_PROG(test_task_map_acquire_release, struct task_struct *task, u64 clone_flags)
{
	struct task_struct *kptr;
	struct __tasks_kfunc_map_value *v;
	long status;

	if (!is_test_kfunc_task())
		return 0;

	status = tasks_kfunc_map_insert(task);
	if (status) {
		err = 1;
		return 0;
	}

	v = tasks_kfunc_map_value_lookup(task);
	if (!v) {
		err = 2;
		return 0;
	}

	bpf_rcu_read_lock();
	kptr = v->task;
	if (!kptr) {
		err = 3;
	} else {
		kptr = bpf_task_acquire(kptr);
		if (!kptr)
			err = 4;
		else
			bpf_task_release(kptr);
	}
	bpf_rcu_read_unlock();

	return 0;
}

SEC("tp_btf/task_newtask")
int BPF_PROG(test_task_current_acquire_release, struct task_struct *task, u64 clone_flags)
{
	struct task_struct *current, *acquired;

	if (!is_test_kfunc_task())
		return 0;

	current = bpf_get_current_task_btf();
	acquired = bpf_task_acquire(current);
	if (acquired)
		bpf_task_release(acquired);
	else
		err = 1;

	return 0;
}

static void lookup_compare_pid(const struct task_struct *p)
{
	struct task_struct *acquired;

	acquired = bpf_task_from_pid(p->pid);
	if (!acquired) {
		err = 1;
		return;
	}

	if (acquired->pid != p->pid)
		err = 2;
	bpf_task_release(acquired);
}

SEC("tp_btf/task_newtask")
int BPF_PROG(test_task_from_pid_arg, struct task_struct *task, u64 clone_flags)
{
	if (!is_test_kfunc_task())
		return 0;

	lookup_compare_pid(task);
	return 0;
}

SEC("tp_btf/task_newtask")
int BPF_PROG(test_task_from_pid_current, struct task_struct *task, u64 clone_flags)
{
	if (!is_test_kfunc_task())
		return 0;

	lookup_compare_pid(bpf_get_current_task_btf());
	return 0;
}

static int is_pid_lookup_valid(s32 pid)
{
	struct task_struct *acquired;

	acquired = bpf_task_from_pid(pid);
	if (acquired) {
		bpf_task_release(acquired);
		return 1;
	}

	return 0;
}

SEC("tp_btf/task_newtask")
int BPF_PROG(test_task_from_pid_invalid, struct task_struct *task, u64 clone_flags)
{
	if (!is_test_kfunc_task())
		return 0;

	bpf_strncmp(task->comm, 12, "foo");
	bpf_strncmp(task->comm, 16, "foo");
	bpf_strncmp(&task->comm[8], 4, "foo");

	if (is_pid_lookup_valid(-1)) {
		err = 1;
		return 0;
	}

	if (is_pid_lookup_valid(0xcafef00d)) {
		err = 2;
		return 0;
	}

	return 0;
}

SEC("tp_btf/task_newtask")
int BPF_PROG(task_kfunc_acquire_trusted_walked, struct task_struct *task, u64 clone_flags)
{
	struct task_struct *acquired;

	/* task->group_leader is listed as a trusted, non-NULL field of task struct. */
	acquired = bpf_task_acquire(task->group_leader);
	if (acquired)
		bpf_task_release(acquired);
	else
		err = 1;


	return 0;
}
