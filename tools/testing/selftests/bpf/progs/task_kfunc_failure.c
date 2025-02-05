// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>

#include "bpf_misc.h"
#include "task_kfunc_common.h"

char _license[] SEC("license") = "GPL";

/* Prototype for all of the program trace events below:
 *
 * TRACE_EVENT(task_newtask,
 *         TP_PROTO(struct task_struct *p, u64 clone_flags)
 */

static struct __tasks_kfunc_map_value *insert_lookup_task(struct task_struct *task)
{
	int status;

	status = tasks_kfunc_map_insert(task);
	if (status)
		return NULL;

	return tasks_kfunc_map_value_lookup(task);
}

SEC("tp_btf/task_newtask")
__failure __msg("Possibly NULL pointer passed to trusted arg0")
int BPF_PROG(task_kfunc_acquire_untrusted, struct task_struct *task, u64 clone_flags)
{
	struct task_struct *acquired;
	struct __tasks_kfunc_map_value *v;

	v = insert_lookup_task(task);
	if (!v)
		return 0;

	/* Can't invoke bpf_task_acquire() on an untrusted pointer. */
	acquired = bpf_task_acquire(v->task);
	if (!acquired)
		return 0;

	bpf_task_release(acquired);

	return 0;
}

SEC("tp_btf/task_newtask")
__failure __msg("arg#0 pointer type STRUCT task_struct must point")
int BPF_PROG(task_kfunc_acquire_fp, struct task_struct *task, u64 clone_flags)
{
	struct task_struct *acquired, *stack_task = (struct task_struct *)&clone_flags;

	/* Can't invoke bpf_task_acquire() on a random frame pointer. */
	acquired = bpf_task_acquire((struct task_struct *)&stack_task);
	if (!acquired)
		return 0;

	bpf_task_release(acquired);

	return 0;
}

SEC("kretprobe/free_task")
__failure __msg("calling kernel function bpf_task_acquire is not allowed")
int BPF_PROG(task_kfunc_acquire_unsafe_kretprobe, struct task_struct *task, u64 clone_flags)
{
	struct task_struct *acquired;

	/* Can't call bpf_task_acquire() or bpf_task_release() in an untrusted prog. */
	acquired = bpf_task_acquire(task);
	if (!acquired)
		return 0;
	bpf_task_release(acquired);

	return 0;
}

SEC("kretprobe/free_task")
__failure __msg("calling kernel function bpf_task_acquire is not allowed")
int BPF_PROG(task_kfunc_acquire_unsafe_kretprobe_rcu, struct task_struct *task, u64 clone_flags)
{
	struct task_struct *acquired;

	bpf_rcu_read_lock();
	if (!task) {
		bpf_rcu_read_unlock();
		return 0;
	}
	/* Can't call bpf_task_acquire() or bpf_task_release() in an untrusted prog. */
	acquired = bpf_task_acquire(task);
	if (acquired)
		bpf_task_release(acquired);
	bpf_rcu_read_unlock();

	return 0;
}

SEC("tp_btf/task_newtask")
__failure __msg("Possibly NULL pointer passed to trusted arg0")
int BPF_PROG(task_kfunc_acquire_null, struct task_struct *task, u64 clone_flags)
{
	struct task_struct *acquired;

	/* Can't invoke bpf_task_acquire() on a NULL pointer. */
	acquired = bpf_task_acquire(NULL);
	if (!acquired)
		return 0;
	bpf_task_release(acquired);

	return 0;
}

SEC("tp_btf/task_newtask")
__failure __msg("Unreleased reference")
int BPF_PROG(task_kfunc_acquire_unreleased, struct task_struct *task, u64 clone_flags)
{
	struct task_struct *acquired;

	acquired = bpf_task_acquire(task);

	/* Acquired task is never released. */
	__sink(acquired);

	return 0;
}

SEC("tp_btf/task_newtask")
__failure __msg("Unreleased reference")
int BPF_PROG(task_kfunc_xchg_unreleased, struct task_struct *task, u64 clone_flags)
{
	struct task_struct *kptr;
	struct __tasks_kfunc_map_value *v;

	v = insert_lookup_task(task);
	if (!v)
		return 0;

	kptr = bpf_kptr_xchg(&v->task, NULL);
	if (!kptr)
		return 0;

	/* Kptr retrieved from map is never released. */

	return 0;
}

SEC("tp_btf/task_newtask")
__failure __msg("Possibly NULL pointer passed to trusted arg0")
int BPF_PROG(task_kfunc_acquire_release_no_null_check, struct task_struct *task, u64 clone_flags)
{
	struct task_struct *acquired;

	acquired = bpf_task_acquire(task);
	/* Can't invoke bpf_task_release() on an acquired task without a NULL check. */
	bpf_task_release(acquired);

	return 0;
}

SEC("tp_btf/task_newtask")
__failure __msg("Possibly NULL pointer passed to trusted arg0")
int BPF_PROG(task_kfunc_release_untrusted, struct task_struct *task, u64 clone_flags)
{
	struct __tasks_kfunc_map_value *v;

	v = insert_lookup_task(task);
	if (!v)
		return 0;

	/* Can't invoke bpf_task_release() on an untrusted pointer. */
	bpf_task_release(v->task);

	return 0;
}

SEC("tp_btf/task_newtask")
__failure __msg("arg#0 pointer type STRUCT task_struct must point")
int BPF_PROG(task_kfunc_release_fp, struct task_struct *task, u64 clone_flags)
{
	struct task_struct *acquired = (struct task_struct *)&clone_flags;

	/* Cannot release random frame pointer. */
	bpf_task_release(acquired);

	return 0;
}

SEC("tp_btf/task_newtask")
__failure __msg("Possibly NULL pointer passed to trusted arg0")
int BPF_PROG(task_kfunc_release_null, struct task_struct *task, u64 clone_flags)
{
	struct __tasks_kfunc_map_value local, *v;
	long status;
	struct task_struct *acquired, *old;
	s32 pid;

	status = bpf_probe_read_kernel(&pid, sizeof(pid), &task->pid);
	if (status)
		return 0;

	local.task = NULL;
	status = bpf_map_update_elem(&__tasks_kfunc_map, &pid, &local, BPF_NOEXIST);
	if (status)
		return status;

	v = bpf_map_lookup_elem(&__tasks_kfunc_map, &pid);
	if (!v)
		return -ENOENT;

	acquired = bpf_task_acquire(task);
	if (!acquired)
		return -EEXIST;

	old = bpf_kptr_xchg(&v->task, acquired);

	/* old cannot be passed to bpf_task_release() without a NULL check. */
	bpf_task_release(old);

	return 0;
}

SEC("tp_btf/task_newtask")
__failure __msg("release kernel function bpf_task_release expects")
int BPF_PROG(task_kfunc_release_unacquired, struct task_struct *task, u64 clone_flags)
{
	/* Cannot release trusted task pointer which was not acquired. */
	bpf_task_release(task);

	return 0;
}

SEC("tp_btf/task_newtask")
__failure __msg("Possibly NULL pointer passed to trusted arg0")
int BPF_PROG(task_kfunc_from_pid_no_null_check, struct task_struct *task, u64 clone_flags)
{
	struct task_struct *acquired;

	acquired = bpf_task_from_pid(task->pid);

	/* Releasing bpf_task_from_pid() lookup without a NULL check. */
	bpf_task_release(acquired);

	return 0;
}

SEC("tp_btf/task_newtask")
__failure __msg("Possibly NULL pointer passed to trusted arg0")
int BPF_PROG(task_kfunc_from_vpid_no_null_check, struct task_struct *task, u64 clone_flags)
{
	struct task_struct *acquired;

	acquired = bpf_task_from_vpid(task->pid);

	/* Releasing bpf_task_from_vpid() lookup without a NULL check. */
	bpf_task_release(acquired);

	return 0;
}

SEC("lsm/task_free")
__failure __msg("R1 must be a rcu pointer")
int BPF_PROG(task_kfunc_from_lsm_task_free, struct task_struct *task)
{
	struct task_struct *acquired;

	/* the argument of lsm task_free hook is untrusted. */
	acquired = bpf_task_acquire(task);
	if (!acquired)
		return 0;

	bpf_task_release(acquired);
	return 0;
}

SEC("tp_btf/task_newtask")
__failure __msg("access beyond the end of member comm")
int BPF_PROG(task_access_comm1, struct task_struct *task, u64 clone_flags)
{
	bpf_strncmp(task->comm, 17, "foo");
	return 0;
}

SEC("tp_btf/task_newtask")
__failure __msg("access beyond the end of member comm")
int BPF_PROG(task_access_comm2, struct task_struct *task, u64 clone_flags)
{
	bpf_strncmp(task->comm + 1, 16, "foo");
	return 0;
}

SEC("tp_btf/task_newtask")
__failure __msg("write into memory")
int BPF_PROG(task_access_comm3, struct task_struct *task, u64 clone_flags)
{
	bpf_probe_read_kernel(task->comm, 16, task->comm);
	return 0;
}

SEC("fentry/__set_task_comm")
__failure __msg("R1 type=ptr_ expected")
int BPF_PROG(task_access_comm4, struct task_struct *task, const char *buf, bool exec)
{
	/*
	 * task->comm is a legacy ptr_to_btf_id. The verifier cannot guarantee
	 * its safety. Hence it cannot be accessed with normal load insns.
	 */
	bpf_strncmp(task->comm, 16, "foo");
	return 0;
}

SEC("tp_btf/task_newtask")
__failure __msg("R1 must be referenced or trusted")
int BPF_PROG(task_kfunc_release_in_map, struct task_struct *task, u64 clone_flags)
{
	struct task_struct *local;
	struct __tasks_kfunc_map_value *v;

	if (tasks_kfunc_map_insert(task))
		return 0;

	v = tasks_kfunc_map_value_lookup(task);
	if (!v)
		return 0;

	bpf_rcu_read_lock();
	local = v->task;
	if (!local) {
		bpf_rcu_read_unlock();
		return 0;
	}
	/* Can't release a kptr that's still stored in a map. */
	bpf_task_release(local);
	bpf_rcu_read_unlock();

	return 0;
}
