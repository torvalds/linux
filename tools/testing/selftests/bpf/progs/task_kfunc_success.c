// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>

#include "task_kfunc_common.h"

char _license[] SEC("license") = "GPL";

int err, pid;

/* Prototype for all of the program trace events below:
 *
 * TRACE_EVENT(task_newtask,
 *         TP_PROTO(struct task_struct *p, u64 clone_flags)
 */

static bool is_test_kfunc_task(void)
{
	int cur_pid = bpf_get_current_pid_tgid() >> 32;

	return pid == cur_pid;
}

static int test_acquire_release(struct task_struct *task)
{
	struct task_struct *acquired;

	acquired = bpf_task_acquire(task);
	bpf_task_release(acquired);

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

	kptr = bpf_kptr_xchg(&v->task, NULL);
	if (!kptr) {
		err = 3;
		return 0;
	}

	bpf_task_release(kptr);

	return 0;
}

SEC("tp_btf/task_newtask")
int BPF_PROG(test_task_get_release, struct task_struct *task, u64 clone_flags)
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

	kptr = bpf_task_kptr_get(&v->task);
	if (kptr) {
		/* Until we resolve the issues with using task->rcu_users, we
		 * expect bpf_task_kptr_get() to return a NULL task. See the
		 * comment at the definition of bpf_task_acquire_not_zero() for
		 * more details.
		 */
		bpf_task_release(kptr);
		err = 3;
		return 0;
	}


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
	bpf_task_release(acquired);

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
	struct task_struct *acquired;

	if (!is_test_kfunc_task())
		return 0;

	lookup_compare_pid(task);
	return 0;
}

SEC("tp_btf/task_newtask")
int BPF_PROG(test_task_from_pid_current, struct task_struct *task, u64 clone_flags)
{
	struct task_struct *current, *acquired;

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
	struct task_struct *acquired;

	if (!is_test_kfunc_task())
		return 0;

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
