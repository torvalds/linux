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

__u32 user_data, target_pid;
__s32 key_serial;
__u64 flags, task_storage_val, cgroup_id;

struct bpf_key *bpf_lookup_user_key(__s32 serial, __u64 flags) __ksym;
void bpf_key_put(struct bpf_key *key) __ksym;
void bpf_rcu_read_lock(void) __ksym;
void bpf_rcu_read_unlock(void) __ksym;
struct task_struct *bpf_task_acquire(struct task_struct *p) __ksym;
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

	/* old style ptr_to_btf_id is not allowed in sleepable */
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
	gparent = bpf_task_acquire(gparent);
	if (!gparent)
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
int BPF_PROG(inproper_sleepable_kfunc, int cmd, union bpf_attr *attr, unsigned int size,
	     bool kernel)
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
int task_trusted_non_rcuptr(void *ctx)
{
	struct task_struct *task, *group_leader;

	task = bpf_get_current_task_btf();
	bpf_rcu_read_lock();
	/* the pointer group_leader is explicitly marked as trusted */
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

__noinline
static int static_subprog(void *ctx)
{
	volatile int ret = 0;

	if (bpf_get_prandom_u32())
		return ret + 42;
	return ret + bpf_get_prandom_u32();
}

__noinline
int global_subprog(u64 a)
{
	volatile int ret = a;

	return ret + static_subprog(NULL);
}

__noinline
static int static_subprog_lock(void *ctx)
{
	volatile int ret = 0;

	bpf_rcu_read_lock();
	if (bpf_get_prandom_u32())
		return ret + 42;
	return ret + bpf_get_prandom_u32();
}

__noinline
int global_subprog_lock(u64 a)
{
	volatile int ret = a;

	return ret + static_subprog_lock(NULL);
}

__noinline
static int static_subprog_unlock(void *ctx)
{
	volatile int ret = 0;

	bpf_rcu_read_unlock();
	if (bpf_get_prandom_u32())
		return ret + 42;
	return ret + bpf_get_prandom_u32();
}

__noinline
int global_subprog_unlock(u64 a)
{
	volatile int ret = a;

	return ret + static_subprog_unlock(NULL);
}

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
int rcu_read_lock_subprog(void *ctx)
{
	volatile int ret = 0;

	bpf_rcu_read_lock();
	if (bpf_get_prandom_u32())
		ret += static_subprog(ctx);
	bpf_rcu_read_unlock();
	return 0;
}

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
int rcu_read_lock_global_subprog(void *ctx)
{
	volatile int ret = 0;

	bpf_rcu_read_lock();
	if (bpf_get_prandom_u32())
		ret += global_subprog(ret);
	bpf_rcu_read_unlock();
	return 0;
}

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
int rcu_read_lock_subprog_lock(void *ctx)
{
	volatile int ret = 0;

	ret += static_subprog_lock(ctx);
	bpf_rcu_read_unlock();
	return 0;
}

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
int rcu_read_lock_global_subprog_lock(void *ctx)
{
	volatile int ret = 0;

	ret += global_subprog_lock(ret);
	bpf_rcu_read_unlock();
	return 0;
}

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
int rcu_read_lock_subprog_unlock(void *ctx)
{
	volatile int ret = 0;

	bpf_rcu_read_lock();
	ret += static_subprog_unlock(ctx);
	return 0;
}

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
int rcu_read_lock_global_subprog_unlock(void *ctx)
{
	volatile int ret = 0;

	bpf_rcu_read_lock();
	ret += global_subprog_unlock(ret);
	return 0;
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

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
int rcu_read_lock_sleepable_helper_global_subprog(void *ctx)
{
	volatile int ret = 0;

	bpf_rcu_read_lock();
	ret += global_sleepable_helper_subprog(ret);
	bpf_rcu_read_unlock();
	return 0;
}

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
int rcu_read_lock_sleepable_kfunc_global_subprog(void *ctx)
{
	volatile int ret = 0;

	bpf_rcu_read_lock();
	ret += global_sleepable_kfunc_subprog(ret);
	bpf_rcu_read_unlock();
	return 0;
}

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
int rcu_read_lock_sleepable_global_subprog_indirect(void *ctx)
{
	volatile int ret = 0;

	bpf_rcu_read_lock();
	ret += global_subprog_calling_sleepable_global(ret);
	bpf_rcu_read_unlock();
	return 0;
}
