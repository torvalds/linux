/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#ifndef _TASK_KFUNC_COMMON_H
#define _TASK_KFUNC_COMMON_H

#include <errno.h>
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

struct __tasks_kfunc_map_value {
	struct task_struct __kptr * task;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, int);
	__type(value, struct __tasks_kfunc_map_value);
	__uint(max_entries, 1);
} __tasks_kfunc_map SEC(".maps");

struct task_struct *bpf_task_acquire(struct task_struct *p) __ksym;
void bpf_task_release(struct task_struct *p) __ksym;
struct task_struct *bpf_task_from_pid(s32 pid) __ksym;
void bpf_rcu_read_lock(void) __ksym;
void bpf_rcu_read_unlock(void) __ksym;

static inline struct __tasks_kfunc_map_value *tasks_kfunc_map_value_lookup(struct task_struct *p)
{
	s32 pid;
	long status;

	status = bpf_probe_read_kernel(&pid, sizeof(pid), &p->pid);
	if (status)
		return NULL;

	return bpf_map_lookup_elem(&__tasks_kfunc_map, &pid);
}

static inline int tasks_kfunc_map_insert(struct task_struct *p)
{
	struct __tasks_kfunc_map_value local, *v;
	long status;
	struct task_struct *acquired, *old;
	s32 pid;

	status = bpf_probe_read_kernel(&pid, sizeof(pid), &p->pid);
	if (status)
		return status;

	local.task = NULL;
	status = bpf_map_update_elem(&__tasks_kfunc_map, &pid, &local, BPF_NOEXIST);
	if (status)
		return status;

	v = bpf_map_lookup_elem(&__tasks_kfunc_map, &pid);
	if (!v) {
		bpf_map_delete_elem(&__tasks_kfunc_map, &pid);
		return -ENOENT;
	}

	acquired = bpf_task_acquire(p);
	if (!acquired)
		return -ENOENT;

	old = bpf_kptr_xchg(&v->task, acquired);
	if (old) {
		bpf_task_release(old);
		return -EEXIST;
	}

	return 0;
}

#endif /* _TASK_KFUNC_COMMON_H */
