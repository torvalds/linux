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
	bpf_task_release(acquired);

	return 0;
}

SEC("kretprobe/free_task")
__failure __msg("reg type unsupported for arg#0 function")
int BPF_PROG(task_kfunc_acquire_unsafe_kretprobe, struct task_struct *task, u64 clone_flags)
{
	struct task_struct *acquired;

	acquired = bpf_task_acquire(task);
	/* Can't release a bpf_task_acquire()'d task without a NULL check. */
	bpf_task_release(acquired);

	return 0;
}

SEC("tp_btf/task_newtask")
__failure __msg("R1 must be referenced or trusted")
int BPF_PROG(task_kfunc_acquire_trusted_walked, struct task_struct *task, u64 clone_flags)
{
	struct task_struct *acquired;

	/* Can't invoke bpf_task_acquire() on a trusted pointer obtained from walking a struct. */
	acquired = bpf_task_acquire(task->group_leader);
	bpf_task_release(acquired);

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

	return 0;
}

SEC("tp_btf/task_newtask")
__failure __msg("arg#0 expected pointer to map value")
int BPF_PROG(task_kfunc_get_non_kptr_param, struct task_struct *task, u64 clone_flags)
{
	struct task_struct *kptr;

	/* Cannot use bpf_task_kptr_get() on a non-kptr, even on a valid task. */
	kptr = bpf_task_kptr_get(&task);
	if (!kptr)
		return 0;

	bpf_task_release(kptr);

	return 0;
}

SEC("tp_btf/task_newtask")
__failure __msg("arg#0 expected pointer to map value")
int BPF_PROG(task_kfunc_get_non_kptr_acquired, struct task_struct *task, u64 clone_flags)
{
	struct task_struct *kptr, *acquired;

	acquired = bpf_task_acquire(task);

	/* Cannot use bpf_task_kptr_get() on a non-kptr, even if it was acquired. */
	kptr = bpf_task_kptr_get(&acquired);
	bpf_task_release(acquired);
	if (!kptr)
		return 0;

	bpf_task_release(kptr);

	return 0;
}

SEC("tp_btf/task_newtask")
__failure __msg("arg#0 expected pointer to map value")
int BPF_PROG(task_kfunc_get_null, struct task_struct *task, u64 clone_flags)
{
	struct task_struct *kptr;

	/* Cannot use bpf_task_kptr_get() on a NULL pointer. */
	kptr = bpf_task_kptr_get(NULL);
	if (!kptr)
		return 0;

	bpf_task_release(kptr);

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
__failure __msg("Unreleased reference")
int BPF_PROG(task_kfunc_get_unreleased, struct task_struct *task, u64 clone_flags)
{
	struct task_struct *kptr;
	struct __tasks_kfunc_map_value *v;

	v = insert_lookup_task(task);
	if (!v)
		return 0;

	kptr = bpf_task_kptr_get(&v->task);
	if (!kptr)
		return 0;

	/* Kptr acquired above is never released. */

	return 0;
}

SEC("tp_btf/task_newtask")
__failure __msg("arg#0 is untrusted_ptr_or_null_ expected ptr_ or socket")
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
__failure __msg("arg#0 is ptr_or_null_ expected ptr_ or socket")
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

	old = bpf_kptr_xchg(&v->task, acquired);

	/* old cannot be passed to bpf_task_release() without a NULL check. */
	bpf_task_release(old);
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
__failure __msg("arg#0 is ptr_or_null_ expected ptr_ or socket")
int BPF_PROG(task_kfunc_from_pid_no_null_check, struct task_struct *task, u64 clone_flags)
{
	struct task_struct *acquired;

	acquired = bpf_task_from_pid(task->pid);

	/* Releasing bpf_task_from_pid() lookup without a NULL check. */
	bpf_task_release(acquired);

	return 0;
}

SEC("lsm/task_free")
__failure __msg("reg type unsupported for arg#0 function")
int BPF_PROG(task_kfunc_from_lsm_task_free, struct task_struct *task)
{
	struct task_struct *acquired;

	/* the argument of lsm task_free hook is untrusted. */
	acquired = bpf_task_acquire(task);
	bpf_task_release(acquired);
	return 0;
}
