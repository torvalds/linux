// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_tracing_net.h"
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, long);
} map_a SEC(".maps");

__u32 user_data, key_serial, target_pid;
__u64 flags, task_storage_val, cgroup_id;

struct bpf_key *bpf_lookup_user_key(__u32 serial, __u64 flags) __ksym;
void bpf_key_put(struct bpf_key *key) __ksym;
void bpf_rcu_read_lock(void) __ksym;
void bpf_rcu_read_unlock(void) __ksym;
struct task_struct *bpf_task_acquire_not_zero(struct task_struct *p) __ksym;
void bpf_task_release(struct task_struct *p) __ksym;

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
int get_cgroup_id(void *ctx)
{
	struct task_struct *task;
	struct css_set *cgroups;

	task = bpf_get_current_task_btf();
	if (task->pid != target_pid)
		return 0;

	/* simulate bpf_get_current_cgroup_id() helper */
	bpf_rcu_read_lock();
	cgroups = task->cgroups;
	if (!cgroups)
		goto unlock;
	cgroup_id = cgroups->dfl_cgrp->kn->id;
unlock:
	bpf_rcu_read_unlock();
	return 0;
}

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
int task_succ(void *ctx)
{
	struct task_struct *task, *real_parent;
	long init_val = 2;
	long *ptr;

	task = bpf_get_current_task_btf();
	if (task->pid != target_pid)
		return 0;

	bpf_rcu_read_lock();
	/* region including helper using rcu ptr real_parent */
	real_parent = task->real_parent;
	if (!real_parent)
		goto out;
	ptr = bpf_task_storage_get(&map_a, real_parent, &init_val,
				   BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (!ptr)
		goto out;
	ptr = bpf_task_storage_get(&map_a, real_parent, 0, 0);
	if (!ptr)
		goto out;
	task_storage_val = *ptr;
out:
	bpf_rcu_read_unlock();
	return 0;
}

SEC("?fentry.s/" SYS_PREFIX "sys_nanosleep")
int no_lock(void *ctx)
{
	struct task_struct *task, *real_parent;

	/* no bpf_rcu_read_lock(), old code still works */
	task = bpf_get_current_task_btf();
	real_parent = task->real_parent;
	(void)bpf_task_storage_get(&map_a, real_parent, 0, 0);
	return 0;
}

SEC("?fentry.s/" SYS_PREFIX "sys_nanosleep")
int two_regions(void *ctx)
{
	struct task_struct *task, *real_parent;

	/* two regions */
	task = bpf_get_current_task_btf();
	bpf_rcu_read_lock();
	bpf_rcu_read_unlock();
	bpf_rcu_read_lock();
	real_parent = task->real_parent;
	if (!real_parent)
		goto out;
	(void)bpf_task_storage_get(&map_a, real_parent, 0, 0);
out:
	bpf_rcu_read_unlock();
	return 0;
}

SEC("?fentry/" SYS_PREFIX "sys_getpgid")
int non_sleepable_1(void *ctx)
{
	struct task_struct *task, *real_parent;

	task = bpf_get_current_task_btf();
	bpf_rcu_read_lock();
	real_parent = task->real_parent;
	if (!real_parent)
		goto out;
	(void)bpf_task_storage_get(&map_a, real_parent, 0, 0);
out:
	bpf_rcu_read_unlock();
	return 0;
}

SEC("?fentry/" SYS_PREFIX "sys_getpgid")
int non_sleepable_2(void *ctx)
{
	struct task_struct *task, *real_parent;

	bpf_rcu_read_lock();
	task = bpf_get_current_task_btf();
	bpf_rcu_read_unlock();

	bpf_rcu_read_lock();
	real_parent = task->real_parent;
	if (!real_parent)
		goto out;
	(void)bpf_task_storage_get(&map_a, real_parent, 0, 0);
out:
	bpf_rcu_read_unlock();
	return 0;
}

SEC("?fentry.s/" SYS_PREFIX "sys_nanosleep")
int task_acquire(void *ctx)
{
	struct task_struct *task, *real_parent, *gparent;

	task = bpf_get_current_task_btf();
	bpf_rcu_read_lock();
	real_parent = task->real_parent;
	if (!real_parent)
		goto out;

	/* rcu_ptr->rcu_field */
	gparent = real_parent->real_parent;
	if (!gparent)
		goto out;

	/* acquire a reference which can be used outside rcu read lock region */
	gparent = bpf_task_acquire_not_zero(gparent);
	if (!gparent)
		/* Until we resolve the issues with using task->rcu_users, we
		 * expect bpf_task_acquire_not_zero() to return a NULL task.
		 * See the comment at the definition of
		 * bpf_task_acquire_not_zero() for more details.
		 */
		goto out;

	(void)bpf_task_storage_get(&map_a, gparent, 0, 0);
	bpf_task_release(gparent);
out:
	bpf_rcu_read_unlock();
	return 0;
}

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
int miss_lock(void *ctx)
{
	struct task_struct *task;
	struct css_set *cgroups;
	struct cgroup *dfl_cgrp;

	/* missing bpf_rcu_read_lock() */
	task = bpf_get_current_task_btf();
	bpf_rcu_read_lock();
	(void)bpf_task_storage_get(&map_a, task, 0, 0);
	bpf_rcu_read_unlock();
	bpf_rcu_read_unlock();
	return 0;
}

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
int miss_unlock(void *ctx)
{
	struct task_struct *task;
	struct css_set *cgroups;
	struct cgroup *dfl_cgrp;

	/* missing bpf_rcu_read_unlock() */
	task = bpf_get_current_task_btf();
	bpf_rcu_read_lock();
	(void)bpf_task_storage_get(&map_a, task, 0, 0);
	return 0;
}

SEC("?fentry/" SYS_PREFIX "sys_getpgid")
int non_sleepable_rcu_mismatch(void *ctx)
{
	struct task_struct *task, *real_parent;

	task = bpf_get_current_task_btf();
	/* non-sleepable: missing bpf_rcu_read_unlock() in one path */
	bpf_rcu_read_lock();
	real_parent = task->real_parent;
	if (!real_parent)
		goto out;
	(void)bpf_task_storage_get(&map_a, real_parent, 0, 0);
	if (real_parent)
		bpf_rcu_read_unlock();
out:
	return 0;
}

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
int inproper_sleepable_helper(void *ctx)
{
	struct task_struct *task, *real_parent;
	struct pt_regs *regs;
	__u32 value = 0;
	void *ptr;

	task = bpf_get_current_task_btf();
	/* sleepable helper in rcu read lock region */
	bpf_rcu_read_lock();
	real_parent = task->real_parent;
	if (!real_parent)
		goto out;
	regs = (struct pt_regs *)bpf_task_pt_regs(real_parent);
	if (!regs)
		goto out;

	ptr = (void *)PT_REGS_IP(regs);
	(void)bpf_copy_from_user_task(&value, sizeof(uint32_t), ptr, task, 0);
	user_data = value;
	(void)bpf_task_storage_get(&map_a, real_parent, 0, 0);
out:
	bpf_rcu_read_unlock();
	return 0;
}

SEC("?lsm.s/bpf")
int BPF_PROG(inproper_sleepable_kfunc, int cmd, union bpf_attr *attr, unsigned int size)
{
	struct bpf_key *bkey;

	/* sleepable kfunc in rcu read lock region */
	bpf_rcu_read_lock();
	bkey = bpf_lookup_user_key(key_serial, flags);
	bpf_rcu_read_unlock();
	if (!bkey)
		return -1;
	bpf_key_put(bkey);

	return 0;
}

SEC("?fentry.s/" SYS_PREFIX "sys_nanosleep")
int nested_rcu_region(void *ctx)
{
	struct task_struct *task, *real_parent;

	/* nested rcu read lock regions */
	task = bpf_get_current_task_btf();
	bpf_rcu_read_lock();
	bpf_rcu_read_lock();
	real_parent = task->real_parent;
	if (!real_parent)
		goto out;
	(void)bpf_task_storage_get(&map_a, real_parent, 0, 0);
out:
	bpf_rcu_read_unlock();
	bpf_rcu_read_unlock();
	return 0;
}

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
int task_untrusted_non_rcuptr(void *ctx)
{
	struct task_struct *task, *group_leader;

	task = bpf_get_current_task_btf();
	bpf_rcu_read_lock();
	/* the pointer group_leader marked as untrusted */
	group_leader = task->real_parent->group_leader;
	(void)bpf_task_storage_get(&map_a, group_leader, 0, 0);
	bpf_rcu_read_unlock();
	return 0;
}

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
int task_untrusted_rcuptr(void *ctx)
{
	struct task_struct *task, *real_parent;

	task = bpf_get_current_task_btf();
	bpf_rcu_read_lock();
	real_parent = task->real_parent;
	bpf_rcu_read_unlock();
	/* helper use of rcu ptr outside the rcu read lock region */
	(void)bpf_task_storage_get(&map_a, real_parent, 0, 0);
	return 0;
}

SEC("?fentry.s/" SYS_PREFIX "sys_nanosleep")
int cross_rcu_region(void *ctx)
{
	struct task_struct *task, *real_parent;

	/* rcu ptr define/use in different regions */
	task = bpf_get_current_task_btf();
	bpf_rcu_read_lock();
	real_parent = task->real_parent;
	bpf_rcu_read_unlock();
	bpf_rcu_read_lock();
	(void)bpf_task_storage_get(&map_a, real_parent, 0, 0);
	bpf_rcu_read_unlock();
	return 0;
}
