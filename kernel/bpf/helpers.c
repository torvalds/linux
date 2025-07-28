// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2011-2014 PLUMgrid, http://plumgrid.com
 */
#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/bpf-cgroup.h>
#include <linux/cgroup.h>
#include <linux/rcupdate.h>
#include <linux/random.h>
#include <linux/smp.h>
#include <linux/topology.h>
#include <linux/ktime.h>
#include <linux/sched.h>
#include <linux/uidgid.h>
#include <linux/filter.h>
#include <linux/ctype.h>
#include <linux/jiffies.h>
#include <linux/pid_namespace.h>
#include <linux/poison.h>
#include <linux/proc_ns.h>
#include <linux/sched/task.h>
#include <linux/security.h>
#include <linux/btf_ids.h>
#include <linux/bpf_mem_alloc.h>
#include <linux/kasan.h>
#include <linux/bpf_verifier.h>

#include "../../lib/kstrtox.h"

/* If kernel subsystem is allowing eBPF programs to call this function,
 * inside its own verifier_ops->get_func_proto() callback it should return
 * bpf_map_lookup_elem_proto, so that verifier can properly check the arguments
 *
 * Different map implementations will rely on rcu in map methods
 * lookup/update/delete, therefore eBPF programs must run under rcu lock
 * if program is allowed to access maps, so check rcu_read_lock_held() or
 * rcu_read_lock_trace_held() in all three functions.
 */
BPF_CALL_2(bpf_map_lookup_elem, struct bpf_map *, map, void *, key)
{
	WARN_ON_ONCE(!rcu_read_lock_held() && !rcu_read_lock_trace_held() &&
		     !rcu_read_lock_bh_held());
	return (unsigned long) map->ops->map_lookup_elem(map, key);
}

const struct bpf_func_proto bpf_map_lookup_elem_proto = {
	.func		= bpf_map_lookup_elem,
	.gpl_only	= false,
	.pkt_access	= true,
	.ret_type	= RET_PTR_TO_MAP_VALUE_OR_NULL,
	.arg1_type	= ARG_CONST_MAP_PTR,
	.arg2_type	= ARG_PTR_TO_MAP_KEY,
};

BPF_CALL_4(bpf_map_update_elem, struct bpf_map *, map, void *, key,
	   void *, value, u64, flags)
{
	WARN_ON_ONCE(!rcu_read_lock_held() && !rcu_read_lock_trace_held() &&
		     !rcu_read_lock_bh_held());
	return map->ops->map_update_elem(map, key, value, flags);
}

const struct bpf_func_proto bpf_map_update_elem_proto = {
	.func		= bpf_map_update_elem,
	.gpl_only	= false,
	.pkt_access	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_CONST_MAP_PTR,
	.arg2_type	= ARG_PTR_TO_MAP_KEY,
	.arg3_type	= ARG_PTR_TO_MAP_VALUE,
	.arg4_type	= ARG_ANYTHING,
};

BPF_CALL_2(bpf_map_delete_elem, struct bpf_map *, map, void *, key)
{
	WARN_ON_ONCE(!rcu_read_lock_held() && !rcu_read_lock_trace_held() &&
		     !rcu_read_lock_bh_held());
	return map->ops->map_delete_elem(map, key);
}

const struct bpf_func_proto bpf_map_delete_elem_proto = {
	.func		= bpf_map_delete_elem,
	.gpl_only	= false,
	.pkt_access	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_CONST_MAP_PTR,
	.arg2_type	= ARG_PTR_TO_MAP_KEY,
};

BPF_CALL_3(bpf_map_push_elem, struct bpf_map *, map, void *, value, u64, flags)
{
	return map->ops->map_push_elem(map, value, flags);
}

const struct bpf_func_proto bpf_map_push_elem_proto = {
	.func		= bpf_map_push_elem,
	.gpl_only	= false,
	.pkt_access	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_CONST_MAP_PTR,
	.arg2_type	= ARG_PTR_TO_MAP_VALUE,
	.arg3_type	= ARG_ANYTHING,
};

BPF_CALL_2(bpf_map_pop_elem, struct bpf_map *, map, void *, value)
{
	return map->ops->map_pop_elem(map, value);
}

const struct bpf_func_proto bpf_map_pop_elem_proto = {
	.func		= bpf_map_pop_elem,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_CONST_MAP_PTR,
	.arg2_type	= ARG_PTR_TO_MAP_VALUE | MEM_UNINIT | MEM_WRITE,
};

BPF_CALL_2(bpf_map_peek_elem, struct bpf_map *, map, void *, value)
{
	return map->ops->map_peek_elem(map, value);
}

const struct bpf_func_proto bpf_map_peek_elem_proto = {
	.func		= bpf_map_peek_elem,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_CONST_MAP_PTR,
	.arg2_type	= ARG_PTR_TO_MAP_VALUE | MEM_UNINIT | MEM_WRITE,
};

BPF_CALL_3(bpf_map_lookup_percpu_elem, struct bpf_map *, map, void *, key, u32, cpu)
{
	WARN_ON_ONCE(!rcu_read_lock_held() && !rcu_read_lock_trace_held() &&
		     !rcu_read_lock_bh_held());
	return (unsigned long) map->ops->map_lookup_percpu_elem(map, key, cpu);
}

const struct bpf_func_proto bpf_map_lookup_percpu_elem_proto = {
	.func		= bpf_map_lookup_percpu_elem,
	.gpl_only	= false,
	.pkt_access	= true,
	.ret_type	= RET_PTR_TO_MAP_VALUE_OR_NULL,
	.arg1_type	= ARG_CONST_MAP_PTR,
	.arg2_type	= ARG_PTR_TO_MAP_KEY,
	.arg3_type	= ARG_ANYTHING,
};

const struct bpf_func_proto bpf_get_prandom_u32_proto = {
	.func		= bpf_user_rnd_u32,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
};

BPF_CALL_0(bpf_get_smp_processor_id)
{
	return smp_processor_id();
}

const struct bpf_func_proto bpf_get_smp_processor_id_proto = {
	.func		= bpf_get_smp_processor_id,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.allow_fastcall	= true,
};

BPF_CALL_0(bpf_get_numa_node_id)
{
	return numa_node_id();
}

const struct bpf_func_proto bpf_get_numa_node_id_proto = {
	.func		= bpf_get_numa_node_id,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
};

BPF_CALL_0(bpf_ktime_get_ns)
{
	/* NMI safe access to clock monotonic */
	return ktime_get_mono_fast_ns();
}

const struct bpf_func_proto bpf_ktime_get_ns_proto = {
	.func		= bpf_ktime_get_ns,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
};

BPF_CALL_0(bpf_ktime_get_boot_ns)
{
	/* NMI safe access to clock boottime */
	return ktime_get_boot_fast_ns();
}

const struct bpf_func_proto bpf_ktime_get_boot_ns_proto = {
	.func		= bpf_ktime_get_boot_ns,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
};

BPF_CALL_0(bpf_ktime_get_coarse_ns)
{
	return ktime_get_coarse_ns();
}

const struct bpf_func_proto bpf_ktime_get_coarse_ns_proto = {
	.func		= bpf_ktime_get_coarse_ns,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
};

BPF_CALL_0(bpf_ktime_get_tai_ns)
{
	/* NMI safe access to clock tai */
	return ktime_get_tai_fast_ns();
}

const struct bpf_func_proto bpf_ktime_get_tai_ns_proto = {
	.func		= bpf_ktime_get_tai_ns,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
};

BPF_CALL_0(bpf_get_current_pid_tgid)
{
	struct task_struct *task = current;

	if (unlikely(!task))
		return -EINVAL;

	return (u64) task->tgid << 32 | task->pid;
}

const struct bpf_func_proto bpf_get_current_pid_tgid_proto = {
	.func		= bpf_get_current_pid_tgid,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
};

BPF_CALL_0(bpf_get_current_uid_gid)
{
	struct task_struct *task = current;
	kuid_t uid;
	kgid_t gid;

	if (unlikely(!task))
		return -EINVAL;

	current_uid_gid(&uid, &gid);
	return (u64) from_kgid(&init_user_ns, gid) << 32 |
		     from_kuid(&init_user_ns, uid);
}

const struct bpf_func_proto bpf_get_current_uid_gid_proto = {
	.func		= bpf_get_current_uid_gid,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
};

BPF_CALL_2(bpf_get_current_comm, char *, buf, u32, size)
{
	struct task_struct *task = current;

	if (unlikely(!task))
		goto err_clear;

	/* Verifier guarantees that size > 0 */
	strscpy_pad(buf, task->comm, size);
	return 0;
err_clear:
	memset(buf, 0, size);
	return -EINVAL;
}

const struct bpf_func_proto bpf_get_current_comm_proto = {
	.func		= bpf_get_current_comm,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg2_type	= ARG_CONST_SIZE,
};

#if defined(CONFIG_QUEUED_SPINLOCKS) || defined(CONFIG_BPF_ARCH_SPINLOCK)

static inline void __bpf_spin_lock(struct bpf_spin_lock *lock)
{
	arch_spinlock_t *l = (void *)lock;
	union {
		__u32 val;
		arch_spinlock_t lock;
	} u = { .lock = __ARCH_SPIN_LOCK_UNLOCKED };

	compiletime_assert(u.val == 0, "__ARCH_SPIN_LOCK_UNLOCKED not 0");
	BUILD_BUG_ON(sizeof(*l) != sizeof(__u32));
	BUILD_BUG_ON(sizeof(*lock) != sizeof(__u32));
	preempt_disable();
	arch_spin_lock(l);
}

static inline void __bpf_spin_unlock(struct bpf_spin_lock *lock)
{
	arch_spinlock_t *l = (void *)lock;

	arch_spin_unlock(l);
	preempt_enable();
}

#else

static inline void __bpf_spin_lock(struct bpf_spin_lock *lock)
{
	atomic_t *l = (void *)lock;

	BUILD_BUG_ON(sizeof(*l) != sizeof(*lock));
	do {
		atomic_cond_read_relaxed(l, !VAL);
	} while (atomic_xchg(l, 1));
}

static inline void __bpf_spin_unlock(struct bpf_spin_lock *lock)
{
	atomic_t *l = (void *)lock;

	atomic_set_release(l, 0);
}

#endif

static DEFINE_PER_CPU(unsigned long, irqsave_flags);

static inline void __bpf_spin_lock_irqsave(struct bpf_spin_lock *lock)
{
	unsigned long flags;

	local_irq_save(flags);
	__bpf_spin_lock(lock);
	__this_cpu_write(irqsave_flags, flags);
}

NOTRACE_BPF_CALL_1(bpf_spin_lock, struct bpf_spin_lock *, lock)
{
	__bpf_spin_lock_irqsave(lock);
	return 0;
}

const struct bpf_func_proto bpf_spin_lock_proto = {
	.func		= bpf_spin_lock,
	.gpl_only	= false,
	.ret_type	= RET_VOID,
	.arg1_type	= ARG_PTR_TO_SPIN_LOCK,
	.arg1_btf_id    = BPF_PTR_POISON,
};

static inline void __bpf_spin_unlock_irqrestore(struct bpf_spin_lock *lock)
{
	unsigned long flags;

	flags = __this_cpu_read(irqsave_flags);
	__bpf_spin_unlock(lock);
	local_irq_restore(flags);
}

NOTRACE_BPF_CALL_1(bpf_spin_unlock, struct bpf_spin_lock *, lock)
{
	__bpf_spin_unlock_irqrestore(lock);
	return 0;
}

const struct bpf_func_proto bpf_spin_unlock_proto = {
	.func		= bpf_spin_unlock,
	.gpl_only	= false,
	.ret_type	= RET_VOID,
	.arg1_type	= ARG_PTR_TO_SPIN_LOCK,
	.arg1_btf_id    = BPF_PTR_POISON,
};

void copy_map_value_locked(struct bpf_map *map, void *dst, void *src,
			   bool lock_src)
{
	struct bpf_spin_lock *lock;

	if (lock_src)
		lock = src + map->record->spin_lock_off;
	else
		lock = dst + map->record->spin_lock_off;
	preempt_disable();
	__bpf_spin_lock_irqsave(lock);
	copy_map_value(map, dst, src);
	__bpf_spin_unlock_irqrestore(lock);
	preempt_enable();
}

BPF_CALL_0(bpf_jiffies64)
{
	return get_jiffies_64();
}

const struct bpf_func_proto bpf_jiffies64_proto = {
	.func		= bpf_jiffies64,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
};

#ifdef CONFIG_CGROUPS
BPF_CALL_0(bpf_get_current_cgroup_id)
{
	struct cgroup *cgrp;
	u64 cgrp_id;

	rcu_read_lock();
	cgrp = task_dfl_cgroup(current);
	cgrp_id = cgroup_id(cgrp);
	rcu_read_unlock();

	return cgrp_id;
}

const struct bpf_func_proto bpf_get_current_cgroup_id_proto = {
	.func		= bpf_get_current_cgroup_id,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
};

BPF_CALL_1(bpf_get_current_ancestor_cgroup_id, int, ancestor_level)
{
	struct cgroup *cgrp;
	struct cgroup *ancestor;
	u64 cgrp_id;

	rcu_read_lock();
	cgrp = task_dfl_cgroup(current);
	ancestor = cgroup_ancestor(cgrp, ancestor_level);
	cgrp_id = ancestor ? cgroup_id(ancestor) : 0;
	rcu_read_unlock();

	return cgrp_id;
}

const struct bpf_func_proto bpf_get_current_ancestor_cgroup_id_proto = {
	.func		= bpf_get_current_ancestor_cgroup_id,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_ANYTHING,
};
#endif /* CONFIG_CGROUPS */

#define BPF_STRTOX_BASE_MASK 0x1F

static int __bpf_strtoull(const char *buf, size_t buf_len, u64 flags,
			  unsigned long long *res, bool *is_negative)
{
	unsigned int base = flags & BPF_STRTOX_BASE_MASK;
	const char *cur_buf = buf;
	size_t cur_len = buf_len;
	unsigned int consumed;
	size_t val_len;
	char str[64];

	if (!buf || !buf_len || !res || !is_negative)
		return -EINVAL;

	if (base != 0 && base != 8 && base != 10 && base != 16)
		return -EINVAL;

	if (flags & ~BPF_STRTOX_BASE_MASK)
		return -EINVAL;

	while (cur_buf < buf + buf_len && isspace(*cur_buf))
		++cur_buf;

	*is_negative = (cur_buf < buf + buf_len && *cur_buf == '-');
	if (*is_negative)
		++cur_buf;

	consumed = cur_buf - buf;
	cur_len -= consumed;
	if (!cur_len)
		return -EINVAL;

	cur_len = min(cur_len, sizeof(str) - 1);
	memcpy(str, cur_buf, cur_len);
	str[cur_len] = '\0';
	cur_buf = str;

	cur_buf = _parse_integer_fixup_radix(cur_buf, &base);
	val_len = _parse_integer(cur_buf, base, res);

	if (val_len & KSTRTOX_OVERFLOW)
		return -ERANGE;

	if (val_len == 0)
		return -EINVAL;

	cur_buf += val_len;
	consumed += cur_buf - str;

	return consumed;
}

static int __bpf_strtoll(const char *buf, size_t buf_len, u64 flags,
			 long long *res)
{
	unsigned long long _res;
	bool is_negative;
	int err;

	err = __bpf_strtoull(buf, buf_len, flags, &_res, &is_negative);
	if (err < 0)
		return err;
	if (is_negative) {
		if ((long long)-_res > 0)
			return -ERANGE;
		*res = -_res;
	} else {
		if ((long long)_res < 0)
			return -ERANGE;
		*res = _res;
	}
	return err;
}

BPF_CALL_4(bpf_strtol, const char *, buf, size_t, buf_len, u64, flags,
	   s64 *, res)
{
	long long _res;
	int err;

	*res = 0;
	err = __bpf_strtoll(buf, buf_len, flags, &_res);
	if (err < 0)
		return err;
	*res = _res;
	return err;
}

const struct bpf_func_proto bpf_strtol_proto = {
	.func		= bpf_strtol,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_MEM | MEM_RDONLY,
	.arg2_type	= ARG_CONST_SIZE,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_PTR_TO_FIXED_SIZE_MEM | MEM_UNINIT | MEM_WRITE | MEM_ALIGNED,
	.arg4_size	= sizeof(s64),
};

BPF_CALL_4(bpf_strtoul, const char *, buf, size_t, buf_len, u64, flags,
	   u64 *, res)
{
	unsigned long long _res;
	bool is_negative;
	int err;

	*res = 0;
	err = __bpf_strtoull(buf, buf_len, flags, &_res, &is_negative);
	if (err < 0)
		return err;
	if (is_negative)
		return -EINVAL;
	*res = _res;
	return err;
}

const struct bpf_func_proto bpf_strtoul_proto = {
	.func		= bpf_strtoul,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_MEM | MEM_RDONLY,
	.arg2_type	= ARG_CONST_SIZE,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_PTR_TO_FIXED_SIZE_MEM | MEM_UNINIT | MEM_WRITE | MEM_ALIGNED,
	.arg4_size	= sizeof(u64),
};

BPF_CALL_3(bpf_strncmp, const char *, s1, u32, s1_sz, const char *, s2)
{
	return strncmp(s1, s2, s1_sz);
}

static const struct bpf_func_proto bpf_strncmp_proto = {
	.func		= bpf_strncmp,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_MEM | MEM_RDONLY,
	.arg2_type	= ARG_CONST_SIZE,
	.arg3_type	= ARG_PTR_TO_CONST_STR,
};

BPF_CALL_4(bpf_get_ns_current_pid_tgid, u64, dev, u64, ino,
	   struct bpf_pidns_info *, nsdata, u32, size)
{
	struct task_struct *task = current;
	struct pid_namespace *pidns;
	int err = -EINVAL;

	if (unlikely(size != sizeof(struct bpf_pidns_info)))
		goto clear;

	if (unlikely((u64)(dev_t)dev != dev))
		goto clear;

	if (unlikely(!task))
		goto clear;

	pidns = task_active_pid_ns(task);
	if (unlikely(!pidns)) {
		err = -ENOENT;
		goto clear;
	}

	if (!ns_match(&pidns->ns, (dev_t)dev, ino))
		goto clear;

	nsdata->pid = task_pid_nr_ns(task, pidns);
	nsdata->tgid = task_tgid_nr_ns(task, pidns);
	return 0;
clear:
	memset((void *)nsdata, 0, (size_t) size);
	return err;
}

const struct bpf_func_proto bpf_get_ns_current_pid_tgid_proto = {
	.func		= bpf_get_ns_current_pid_tgid,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_ANYTHING,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type      = ARG_PTR_TO_UNINIT_MEM,
	.arg4_type      = ARG_CONST_SIZE,
};

static const struct bpf_func_proto bpf_get_raw_smp_processor_id_proto = {
	.func		= bpf_get_raw_cpu_id,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
};

BPF_CALL_5(bpf_event_output_data, void *, ctx, struct bpf_map *, map,
	   u64, flags, void *, data, u64, size)
{
	if (unlikely(flags & ~(BPF_F_INDEX_MASK)))
		return -EINVAL;

	return bpf_event_output(map, flags, data, size, NULL, 0, NULL);
}

const struct bpf_func_proto bpf_event_output_data_proto =  {
	.func		= bpf_event_output_data,
	.gpl_only       = true,
	.ret_type       = RET_INTEGER,
	.arg1_type      = ARG_PTR_TO_CTX,
	.arg2_type      = ARG_CONST_MAP_PTR,
	.arg3_type      = ARG_ANYTHING,
	.arg4_type      = ARG_PTR_TO_MEM | MEM_RDONLY,
	.arg5_type      = ARG_CONST_SIZE_OR_ZERO,
};

BPF_CALL_3(bpf_copy_from_user, void *, dst, u32, size,
	   const void __user *, user_ptr)
{
	int ret = copy_from_user(dst, user_ptr, size);

	if (unlikely(ret)) {
		memset(dst, 0, size);
		ret = -EFAULT;
	}

	return ret;
}

const struct bpf_func_proto bpf_copy_from_user_proto = {
	.func		= bpf_copy_from_user,
	.gpl_only	= false,
	.might_sleep	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg2_type	= ARG_CONST_SIZE_OR_ZERO,
	.arg3_type	= ARG_ANYTHING,
};

BPF_CALL_5(bpf_copy_from_user_task, void *, dst, u32, size,
	   const void __user *, user_ptr, struct task_struct *, tsk, u64, flags)
{
	int ret;

	/* flags is not used yet */
	if (unlikely(flags))
		return -EINVAL;

	if (unlikely(!size))
		return 0;

	ret = access_process_vm(tsk, (unsigned long)user_ptr, dst, size, 0);
	if (ret == size)
		return 0;

	memset(dst, 0, size);
	/* Return -EFAULT for partial read */
	return ret < 0 ? ret : -EFAULT;
}

const struct bpf_func_proto bpf_copy_from_user_task_proto = {
	.func		= bpf_copy_from_user_task,
	.gpl_only	= true,
	.might_sleep	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg2_type	= ARG_CONST_SIZE_OR_ZERO,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_PTR_TO_BTF_ID,
	.arg4_btf_id	= &btf_tracing_ids[BTF_TRACING_TYPE_TASK],
	.arg5_type	= ARG_ANYTHING
};

BPF_CALL_2(bpf_per_cpu_ptr, const void *, ptr, u32, cpu)
{
	if (cpu >= nr_cpu_ids)
		return (unsigned long)NULL;

	return (unsigned long)per_cpu_ptr((const void __percpu *)(const uintptr_t)ptr, cpu);
}

const struct bpf_func_proto bpf_per_cpu_ptr_proto = {
	.func		= bpf_per_cpu_ptr,
	.gpl_only	= false,
	.ret_type	= RET_PTR_TO_MEM_OR_BTF_ID | PTR_MAYBE_NULL | MEM_RDONLY,
	.arg1_type	= ARG_PTR_TO_PERCPU_BTF_ID,
	.arg2_type	= ARG_ANYTHING,
};

BPF_CALL_1(bpf_this_cpu_ptr, const void *, percpu_ptr)
{
	return (unsigned long)this_cpu_ptr((const void __percpu *)(const uintptr_t)percpu_ptr);
}

const struct bpf_func_proto bpf_this_cpu_ptr_proto = {
	.func		= bpf_this_cpu_ptr,
	.gpl_only	= false,
	.ret_type	= RET_PTR_TO_MEM_OR_BTF_ID | MEM_RDONLY,
	.arg1_type	= ARG_PTR_TO_PERCPU_BTF_ID,
};

static int bpf_trace_copy_string(char *buf, void *unsafe_ptr, char fmt_ptype,
		size_t bufsz)
{
	void __user *user_ptr = (__force void __user *)unsafe_ptr;

	buf[0] = 0;

	switch (fmt_ptype) {
	case 's':
#ifdef CONFIG_ARCH_HAS_NON_OVERLAPPING_ADDRESS_SPACE
		if ((unsigned long)unsafe_ptr < TASK_SIZE)
			return strncpy_from_user_nofault(buf, user_ptr, bufsz);
		fallthrough;
#endif
	case 'k':
		return strncpy_from_kernel_nofault(buf, unsafe_ptr, bufsz);
	case 'u':
		return strncpy_from_user_nofault(buf, user_ptr, bufsz);
	}

	return -EINVAL;
}

/* Per-cpu temp buffers used by printf-like helpers to store the bprintf binary
 * arguments representation.
 */
#define MAX_BPRINTF_BIN_ARGS	512

/* Support executing three nested bprintf helper calls on a given CPU */
#define MAX_BPRINTF_NEST_LEVEL	3
struct bpf_bprintf_buffers {
	char bin_args[MAX_BPRINTF_BIN_ARGS];
	char buf[MAX_BPRINTF_BUF];
};

static DEFINE_PER_CPU(struct bpf_bprintf_buffers[MAX_BPRINTF_NEST_LEVEL], bpf_bprintf_bufs);
static DEFINE_PER_CPU(int, bpf_bprintf_nest_level);

static int try_get_buffers(struct bpf_bprintf_buffers **bufs)
{
	int nest_level;

	preempt_disable();
	nest_level = this_cpu_inc_return(bpf_bprintf_nest_level);
	if (WARN_ON_ONCE(nest_level > MAX_BPRINTF_NEST_LEVEL)) {
		this_cpu_dec(bpf_bprintf_nest_level);
		preempt_enable();
		return -EBUSY;
	}
	*bufs = this_cpu_ptr(&bpf_bprintf_bufs[nest_level - 1]);

	return 0;
}

void bpf_bprintf_cleanup(struct bpf_bprintf_data *data)
{
	if (!data->bin_args && !data->buf)
		return;
	if (WARN_ON_ONCE(this_cpu_read(bpf_bprintf_nest_level) == 0))
		return;
	this_cpu_dec(bpf_bprintf_nest_level);
	preempt_enable();
}

/*
 * bpf_bprintf_prepare - Generic pass on format strings for bprintf-like helpers
 *
 * Returns a negative value if fmt is an invalid format string or 0 otherwise.
 *
 * This can be used in two ways:
 * - Format string verification only: when data->get_bin_args is false
 * - Arguments preparation: in addition to the above verification, it writes in
 *   data->bin_args a binary representation of arguments usable by bstr_printf
 *   where pointers from BPF have been sanitized.
 *
 * In argument preparation mode, if 0 is returned, safe temporary buffers are
 * allocated and bpf_bprintf_cleanup should be called to free them after use.
 */
int bpf_bprintf_prepare(char *fmt, u32 fmt_size, const u64 *raw_args,
			u32 num_args, struct bpf_bprintf_data *data)
{
	bool get_buffers = (data->get_bin_args && num_args) || data->get_buf;
	char *unsafe_ptr = NULL, *tmp_buf = NULL, *tmp_buf_end, *fmt_end;
	struct bpf_bprintf_buffers *buffers = NULL;
	size_t sizeof_cur_arg, sizeof_cur_ip;
	int err, i, num_spec = 0;
	u64 cur_arg;
	char fmt_ptype, cur_ip[16], ip_spec[] = "%pXX";

	fmt_end = strnchr(fmt, fmt_size, 0);
	if (!fmt_end)
		return -EINVAL;
	fmt_size = fmt_end - fmt;

	if (get_buffers && try_get_buffers(&buffers))
		return -EBUSY;

	if (data->get_bin_args) {
		if (num_args)
			tmp_buf = buffers->bin_args;
		tmp_buf_end = tmp_buf + MAX_BPRINTF_BIN_ARGS;
		data->bin_args = (u32 *)tmp_buf;
	}

	if (data->get_buf)
		data->buf = buffers->buf;

	for (i = 0; i < fmt_size; i++) {
		if ((!isprint(fmt[i]) && !isspace(fmt[i])) || !isascii(fmt[i])) {
			err = -EINVAL;
			goto out;
		}

		if (fmt[i] != '%')
			continue;

		if (fmt[i + 1] == '%') {
			i++;
			continue;
		}

		if (num_spec >= num_args) {
			err = -EINVAL;
			goto out;
		}

		/* The string is zero-terminated so if fmt[i] != 0, we can
		 * always access fmt[i + 1], in the worst case it will be a 0
		 */
		i++;

		/* skip optional "[0 +-][num]" width formatting field */
		while (fmt[i] == '0' || fmt[i] == '+'  || fmt[i] == '-' ||
		       fmt[i] == ' ')
			i++;
		if (fmt[i] >= '1' && fmt[i] <= '9') {
			i++;
			while (fmt[i] >= '0' && fmt[i] <= '9')
				i++;
		}

		if (fmt[i] == 'p') {
			sizeof_cur_arg = sizeof(long);

			if (fmt[i + 1] == 0 || isspace(fmt[i + 1]) ||
			    ispunct(fmt[i + 1])) {
				if (tmp_buf)
					cur_arg = raw_args[num_spec];
				goto nocopy_fmt;
			}

			if ((fmt[i + 1] == 'k' || fmt[i + 1] == 'u') &&
			    fmt[i + 2] == 's') {
				fmt_ptype = fmt[i + 1];
				i += 2;
				goto fmt_str;
			}

			if (fmt[i + 1] == 'K' ||
			    fmt[i + 1] == 'x' || fmt[i + 1] == 's' ||
			    fmt[i + 1] == 'S') {
				if (tmp_buf)
					cur_arg = raw_args[num_spec];
				i++;
				goto nocopy_fmt;
			}

			if (fmt[i + 1] == 'B') {
				if (tmp_buf)  {
					err = snprintf(tmp_buf,
						       (tmp_buf_end - tmp_buf),
						       "%pB",
						       (void *)(long)raw_args[num_spec]);
					tmp_buf += (err + 1);
				}

				i++;
				num_spec++;
				continue;
			}

			/* only support "%pI4", "%pi4", "%pI6" and "%pi6". */
			if ((fmt[i + 1] != 'i' && fmt[i + 1] != 'I') ||
			    (fmt[i + 2] != '4' && fmt[i + 2] != '6')) {
				err = -EINVAL;
				goto out;
			}

			i += 2;
			if (!tmp_buf)
				goto nocopy_fmt;

			sizeof_cur_ip = (fmt[i] == '4') ? 4 : 16;
			if (tmp_buf_end - tmp_buf < sizeof_cur_ip) {
				err = -ENOSPC;
				goto out;
			}

			unsafe_ptr = (char *)(long)raw_args[num_spec];
			err = copy_from_kernel_nofault(cur_ip, unsafe_ptr,
						       sizeof_cur_ip);
			if (err < 0)
				memset(cur_ip, 0, sizeof_cur_ip);

			/* hack: bstr_printf expects IP addresses to be
			 * pre-formatted as strings, ironically, the easiest way
			 * to do that is to call snprintf.
			 */
			ip_spec[2] = fmt[i - 1];
			ip_spec[3] = fmt[i];
			err = snprintf(tmp_buf, tmp_buf_end - tmp_buf,
				       ip_spec, &cur_ip);

			tmp_buf += err + 1;
			num_spec++;

			continue;
		} else if (fmt[i] == 's') {
			fmt_ptype = fmt[i];
fmt_str:
			if (fmt[i + 1] != 0 &&
			    !isspace(fmt[i + 1]) &&
			    !ispunct(fmt[i + 1])) {
				err = -EINVAL;
				goto out;
			}

			if (!tmp_buf)
				goto nocopy_fmt;

			if (tmp_buf_end == tmp_buf) {
				err = -ENOSPC;
				goto out;
			}

			unsafe_ptr = (char *)(long)raw_args[num_spec];
			err = bpf_trace_copy_string(tmp_buf, unsafe_ptr,
						    fmt_ptype,
						    tmp_buf_end - tmp_buf);
			if (err < 0) {
				tmp_buf[0] = '\0';
				err = 1;
			}

			tmp_buf += err;
			num_spec++;

			continue;
		} else if (fmt[i] == 'c') {
			if (!tmp_buf)
				goto nocopy_fmt;

			if (tmp_buf_end == tmp_buf) {
				err = -ENOSPC;
				goto out;
			}

			*tmp_buf = raw_args[num_spec];
			tmp_buf++;
			num_spec++;

			continue;
		}

		sizeof_cur_arg = sizeof(int);

		if (fmt[i] == 'l') {
			sizeof_cur_arg = sizeof(long);
			i++;
		}
		if (fmt[i] == 'l') {
			sizeof_cur_arg = sizeof(long long);
			i++;
		}

		if (fmt[i] != 'i' && fmt[i] != 'd' && fmt[i] != 'u' &&
		    fmt[i] != 'x' && fmt[i] != 'X') {
			err = -EINVAL;
			goto out;
		}

		if (tmp_buf)
			cur_arg = raw_args[num_spec];
nocopy_fmt:
		if (tmp_buf) {
			tmp_buf = PTR_ALIGN(tmp_buf, sizeof(u32));
			if (tmp_buf_end - tmp_buf < sizeof_cur_arg) {
				err = -ENOSPC;
				goto out;
			}

			if (sizeof_cur_arg == 8) {
				*(u32 *)tmp_buf = *(u32 *)&cur_arg;
				*(u32 *)(tmp_buf + 4) = *((u32 *)&cur_arg + 1);
			} else {
				*(u32 *)tmp_buf = (u32)(long)cur_arg;
			}
			tmp_buf += sizeof_cur_arg;
		}
		num_spec++;
	}

	err = 0;
out:
	if (err)
		bpf_bprintf_cleanup(data);
	return err;
}

BPF_CALL_5(bpf_snprintf, char *, str, u32, str_size, char *, fmt,
	   const void *, args, u32, data_len)
{
	struct bpf_bprintf_data data = {
		.get_bin_args	= true,
	};
	int err, num_args;

	if (data_len % 8 || data_len > MAX_BPRINTF_VARARGS * 8 ||
	    (data_len && !args))
		return -EINVAL;
	num_args = data_len / 8;

	/* ARG_PTR_TO_CONST_STR guarantees that fmt is zero-terminated so we
	 * can safely give an unbounded size.
	 */
	err = bpf_bprintf_prepare(fmt, UINT_MAX, args, num_args, &data);
	if (err < 0)
		return err;

	err = bstr_printf(str, str_size, fmt, data.bin_args);

	bpf_bprintf_cleanup(&data);

	return err + 1;
}

const struct bpf_func_proto bpf_snprintf_proto = {
	.func		= bpf_snprintf,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_MEM_OR_NULL,
	.arg2_type	= ARG_CONST_SIZE_OR_ZERO,
	.arg3_type	= ARG_PTR_TO_CONST_STR,
	.arg4_type	= ARG_PTR_TO_MEM | PTR_MAYBE_NULL | MEM_RDONLY,
	.arg5_type	= ARG_CONST_SIZE_OR_ZERO,
};

struct bpf_async_cb {
	struct bpf_map *map;
	struct bpf_prog *prog;
	void __rcu *callback_fn;
	void *value;
	union {
		struct rcu_head rcu;
		struct work_struct delete_work;
	};
	u64 flags;
};

/* BPF map elements can contain 'struct bpf_timer'.
 * Such map owns all of its BPF timers.
 * 'struct bpf_timer' is allocated as part of map element allocation
 * and it's zero initialized.
 * That space is used to keep 'struct bpf_async_kern'.
 * bpf_timer_init() allocates 'struct bpf_hrtimer', inits hrtimer, and
 * remembers 'struct bpf_map *' pointer it's part of.
 * bpf_timer_set_callback() increments prog refcnt and assign bpf callback_fn.
 * bpf_timer_start() arms the timer.
 * If user space reference to a map goes to zero at this point
 * ops->map_release_uref callback is responsible for cancelling the timers,
 * freeing their memory, and decrementing prog's refcnts.
 * bpf_timer_cancel() cancels the timer and decrements prog's refcnt.
 * Inner maps can contain bpf timers as well. ops->map_release_uref is
 * freeing the timers when inner map is replaced or deleted by user space.
 */
struct bpf_hrtimer {
	struct bpf_async_cb cb;
	struct hrtimer timer;
	atomic_t cancelling;
};

struct bpf_work {
	struct bpf_async_cb cb;
	struct work_struct work;
	struct work_struct delete_work;
};

/* the actual struct hidden inside uapi struct bpf_timer and bpf_wq */
struct bpf_async_kern {
	union {
		struct bpf_async_cb *cb;
		struct bpf_hrtimer *timer;
		struct bpf_work *work;
	};
	/* bpf_spin_lock is used here instead of spinlock_t to make
	 * sure that it always fits into space reserved by struct bpf_timer
	 * regardless of LOCKDEP and spinlock debug flags.
	 */
	struct bpf_spin_lock lock;
} __attribute__((aligned(8)));

enum bpf_async_type {
	BPF_ASYNC_TYPE_TIMER = 0,
	BPF_ASYNC_TYPE_WQ,
};

static DEFINE_PER_CPU(struct bpf_hrtimer *, hrtimer_running);

static enum hrtimer_restart bpf_timer_cb(struct hrtimer *hrtimer)
{
	struct bpf_hrtimer *t = container_of(hrtimer, struct bpf_hrtimer, timer);
	struct bpf_map *map = t->cb.map;
	void *value = t->cb.value;
	bpf_callback_t callback_fn;
	void *key;
	u32 idx;

	BTF_TYPE_EMIT(struct bpf_timer);
	callback_fn = rcu_dereference_check(t->cb.callback_fn, rcu_read_lock_bh_held());
	if (!callback_fn)
		goto out;

	/* bpf_timer_cb() runs in hrtimer_run_softirq. It doesn't migrate and
	 * cannot be preempted by another bpf_timer_cb() on the same cpu.
	 * Remember the timer this callback is servicing to prevent
	 * deadlock if callback_fn() calls bpf_timer_cancel() or
	 * bpf_map_delete_elem() on the same timer.
	 */
	this_cpu_write(hrtimer_running, t);
	if (map->map_type == BPF_MAP_TYPE_ARRAY) {
		struct bpf_array *array = container_of(map, struct bpf_array, map);

		/* compute the key */
		idx = ((char *)value - array->value) / array->elem_size;
		key = &idx;
	} else { /* hash or lru */
		key = value - round_up(map->key_size, 8);
	}

	callback_fn((u64)(long)map, (u64)(long)key, (u64)(long)value, 0, 0);
	/* The verifier checked that return value is zero. */

	this_cpu_write(hrtimer_running, NULL);
out:
	return HRTIMER_NORESTART;
}

static void bpf_wq_work(struct work_struct *work)
{
	struct bpf_work *w = container_of(work, struct bpf_work, work);
	struct bpf_async_cb *cb = &w->cb;
	struct bpf_map *map = cb->map;
	bpf_callback_t callback_fn;
	void *value = cb->value;
	void *key;
	u32 idx;

	BTF_TYPE_EMIT(struct bpf_wq);

	callback_fn = READ_ONCE(cb->callback_fn);
	if (!callback_fn)
		return;

	if (map->map_type == BPF_MAP_TYPE_ARRAY) {
		struct bpf_array *array = container_of(map, struct bpf_array, map);

		/* compute the key */
		idx = ((char *)value - array->value) / array->elem_size;
		key = &idx;
	} else { /* hash or lru */
		key = value - round_up(map->key_size, 8);
	}

        rcu_read_lock_trace();
        migrate_disable();

	callback_fn((u64)(long)map, (u64)(long)key, (u64)(long)value, 0, 0);

	migrate_enable();
	rcu_read_unlock_trace();
}

static void bpf_wq_delete_work(struct work_struct *work)
{
	struct bpf_work *w = container_of(work, struct bpf_work, delete_work);

	cancel_work_sync(&w->work);

	kfree_rcu(w, cb.rcu);
}

static void bpf_timer_delete_work(struct work_struct *work)
{
	struct bpf_hrtimer *t = container_of(work, struct bpf_hrtimer, cb.delete_work);

	/* Cancel the timer and wait for callback to complete if it was running.
	 * If hrtimer_cancel() can be safely called it's safe to call
	 * kfree_rcu(t) right after for both preallocated and non-preallocated
	 * maps.  The async->cb = NULL was already done and no code path can see
	 * address 't' anymore. Timer if armed for existing bpf_hrtimer before
	 * bpf_timer_cancel_and_free will have been cancelled.
	 */
	hrtimer_cancel(&t->timer);
	kfree_rcu(t, cb.rcu);
}

static int __bpf_async_init(struct bpf_async_kern *async, struct bpf_map *map, u64 flags,
			    enum bpf_async_type type)
{
	struct bpf_async_cb *cb;
	struct bpf_hrtimer *t;
	struct bpf_work *w;
	clockid_t clockid;
	size_t size;
	int ret = 0;

	if (in_nmi())
		return -EOPNOTSUPP;

	switch (type) {
	case BPF_ASYNC_TYPE_TIMER:
		size = sizeof(struct bpf_hrtimer);
		break;
	case BPF_ASYNC_TYPE_WQ:
		size = sizeof(struct bpf_work);
		break;
	default:
		return -EINVAL;
	}

	__bpf_spin_lock_irqsave(&async->lock);
	t = async->timer;
	if (t) {
		ret = -EBUSY;
		goto out;
	}

	/* allocate hrtimer via map_kmalloc to use memcg accounting */
	cb = bpf_map_kmalloc_node(map, size, GFP_ATOMIC, map->numa_node);
	if (!cb) {
		ret = -ENOMEM;
		goto out;
	}

	switch (type) {
	case BPF_ASYNC_TYPE_TIMER:
		clockid = flags & (MAX_CLOCKS - 1);
		t = (struct bpf_hrtimer *)cb;

		atomic_set(&t->cancelling, 0);
		INIT_WORK(&t->cb.delete_work, bpf_timer_delete_work);
		hrtimer_setup(&t->timer, bpf_timer_cb, clockid, HRTIMER_MODE_REL_SOFT);
		cb->value = (void *)async - map->record->timer_off;
		break;
	case BPF_ASYNC_TYPE_WQ:
		w = (struct bpf_work *)cb;

		INIT_WORK(&w->work, bpf_wq_work);
		INIT_WORK(&w->delete_work, bpf_wq_delete_work);
		cb->value = (void *)async - map->record->wq_off;
		break;
	}
	cb->map = map;
	cb->prog = NULL;
	cb->flags = flags;
	rcu_assign_pointer(cb->callback_fn, NULL);

	WRITE_ONCE(async->cb, cb);
	/* Guarantee the order between async->cb and map->usercnt. So
	 * when there are concurrent uref release and bpf timer init, either
	 * bpf_timer_cancel_and_free() called by uref release reads a no-NULL
	 * timer or atomic64_read() below returns a zero usercnt.
	 */
	smp_mb();
	if (!atomic64_read(&map->usercnt)) {
		/* maps with timers must be either held by user space
		 * or pinned in bpffs.
		 */
		WRITE_ONCE(async->cb, NULL);
		kfree(cb);
		ret = -EPERM;
	}
out:
	__bpf_spin_unlock_irqrestore(&async->lock);
	return ret;
}

BPF_CALL_3(bpf_timer_init, struct bpf_async_kern *, timer, struct bpf_map *, map,
	   u64, flags)
{
	clock_t clockid = flags & (MAX_CLOCKS - 1);

	BUILD_BUG_ON(MAX_CLOCKS != 16);
	BUILD_BUG_ON(sizeof(struct bpf_async_kern) > sizeof(struct bpf_timer));
	BUILD_BUG_ON(__alignof__(struct bpf_async_kern) != __alignof__(struct bpf_timer));

	if (flags >= MAX_CLOCKS ||
	    /* similar to timerfd except _ALARM variants are not supported */
	    (clockid != CLOCK_MONOTONIC &&
	     clockid != CLOCK_REALTIME &&
	     clockid != CLOCK_BOOTTIME))
		return -EINVAL;

	return __bpf_async_init(timer, map, flags, BPF_ASYNC_TYPE_TIMER);
}

static const struct bpf_func_proto bpf_timer_init_proto = {
	.func		= bpf_timer_init,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_TIMER,
	.arg2_type	= ARG_CONST_MAP_PTR,
	.arg3_type	= ARG_ANYTHING,
};

static int __bpf_async_set_callback(struct bpf_async_kern *async, void *callback_fn,
				    struct bpf_prog_aux *aux, unsigned int flags,
				    enum bpf_async_type type)
{
	struct bpf_prog *prev, *prog = aux->prog;
	struct bpf_async_cb *cb;
	int ret = 0;

	if (in_nmi())
		return -EOPNOTSUPP;
	__bpf_spin_lock_irqsave(&async->lock);
	cb = async->cb;
	if (!cb) {
		ret = -EINVAL;
		goto out;
	}
	if (!atomic64_read(&cb->map->usercnt)) {
		/* maps with timers must be either held by user space
		 * or pinned in bpffs. Otherwise timer might still be
		 * running even when bpf prog is detached and user space
		 * is gone, since map_release_uref won't ever be called.
		 */
		ret = -EPERM;
		goto out;
	}
	prev = cb->prog;
	if (prev != prog) {
		/* Bump prog refcnt once. Every bpf_timer_set_callback()
		 * can pick different callback_fn-s within the same prog.
		 */
		prog = bpf_prog_inc_not_zero(prog);
		if (IS_ERR(prog)) {
			ret = PTR_ERR(prog);
			goto out;
		}
		if (prev)
			/* Drop prev prog refcnt when swapping with new prog */
			bpf_prog_put(prev);
		cb->prog = prog;
	}
	rcu_assign_pointer(cb->callback_fn, callback_fn);
out:
	__bpf_spin_unlock_irqrestore(&async->lock);
	return ret;
}

BPF_CALL_3(bpf_timer_set_callback, struct bpf_async_kern *, timer, void *, callback_fn,
	   struct bpf_prog_aux *, aux)
{
	return __bpf_async_set_callback(timer, callback_fn, aux, 0, BPF_ASYNC_TYPE_TIMER);
}

static const struct bpf_func_proto bpf_timer_set_callback_proto = {
	.func		= bpf_timer_set_callback,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_TIMER,
	.arg2_type	= ARG_PTR_TO_FUNC,
};

BPF_CALL_3(bpf_timer_start, struct bpf_async_kern *, timer, u64, nsecs, u64, flags)
{
	struct bpf_hrtimer *t;
	int ret = 0;
	enum hrtimer_mode mode;

	if (in_nmi())
		return -EOPNOTSUPP;
	if (flags & ~(BPF_F_TIMER_ABS | BPF_F_TIMER_CPU_PIN))
		return -EINVAL;
	__bpf_spin_lock_irqsave(&timer->lock);
	t = timer->timer;
	if (!t || !t->cb.prog) {
		ret = -EINVAL;
		goto out;
	}

	if (flags & BPF_F_TIMER_ABS)
		mode = HRTIMER_MODE_ABS_SOFT;
	else
		mode = HRTIMER_MODE_REL_SOFT;

	if (flags & BPF_F_TIMER_CPU_PIN)
		mode |= HRTIMER_MODE_PINNED;

	hrtimer_start(&t->timer, ns_to_ktime(nsecs), mode);
out:
	__bpf_spin_unlock_irqrestore(&timer->lock);
	return ret;
}

static const struct bpf_func_proto bpf_timer_start_proto = {
	.func		= bpf_timer_start,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_TIMER,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_ANYTHING,
};

static void drop_prog_refcnt(struct bpf_async_cb *async)
{
	struct bpf_prog *prog = async->prog;

	if (prog) {
		bpf_prog_put(prog);
		async->prog = NULL;
		rcu_assign_pointer(async->callback_fn, NULL);
	}
}

BPF_CALL_1(bpf_timer_cancel, struct bpf_async_kern *, timer)
{
	struct bpf_hrtimer *t, *cur_t;
	bool inc = false;
	int ret = 0;

	if (in_nmi())
		return -EOPNOTSUPP;
	rcu_read_lock();
	__bpf_spin_lock_irqsave(&timer->lock);
	t = timer->timer;
	if (!t) {
		ret = -EINVAL;
		goto out;
	}

	cur_t = this_cpu_read(hrtimer_running);
	if (cur_t == t) {
		/* If bpf callback_fn is trying to bpf_timer_cancel()
		 * its own timer the hrtimer_cancel() will deadlock
		 * since it waits for callback_fn to finish.
		 */
		ret = -EDEADLK;
		goto out;
	}

	/* Only account in-flight cancellations when invoked from a timer
	 * callback, since we want to avoid waiting only if other _callbacks_
	 * are waiting on us, to avoid introducing lockups. Non-callback paths
	 * are ok, since nobody would synchronously wait for their completion.
	 */
	if (!cur_t)
		goto drop;
	atomic_inc(&t->cancelling);
	/* Need full barrier after relaxed atomic_inc */
	smp_mb__after_atomic();
	inc = true;
	if (atomic_read(&cur_t->cancelling)) {
		/* We're cancelling timer t, while some other timer callback is
		 * attempting to cancel us. In such a case, it might be possible
		 * that timer t belongs to the other callback, or some other
		 * callback waiting upon it (creating transitive dependencies
		 * upon us), and we will enter a deadlock if we continue
		 * cancelling and waiting for it synchronously, since it might
		 * do the same. Bail!
		 */
		ret = -EDEADLK;
		goto out;
	}
drop:
	drop_prog_refcnt(&t->cb);
out:
	__bpf_spin_unlock_irqrestore(&timer->lock);
	/* Cancel the timer and wait for associated callback to finish
	 * if it was running.
	 */
	ret = ret ?: hrtimer_cancel(&t->timer);
	if (inc)
		atomic_dec(&t->cancelling);
	rcu_read_unlock();
	return ret;
}

static const struct bpf_func_proto bpf_timer_cancel_proto = {
	.func		= bpf_timer_cancel,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_TIMER,
};

static struct bpf_async_cb *__bpf_async_cancel_and_free(struct bpf_async_kern *async)
{
	struct bpf_async_cb *cb;

	/* Performance optimization: read async->cb without lock first. */
	if (!READ_ONCE(async->cb))
		return NULL;

	__bpf_spin_lock_irqsave(&async->lock);
	/* re-read it under lock */
	cb = async->cb;
	if (!cb)
		goto out;
	drop_prog_refcnt(cb);
	/* The subsequent bpf_timer_start/cancel() helpers won't be able to use
	 * this timer, since it won't be initialized.
	 */
	WRITE_ONCE(async->cb, NULL);
out:
	__bpf_spin_unlock_irqrestore(&async->lock);
	return cb;
}

/* This function is called by map_delete/update_elem for individual element and
 * by ops->map_release_uref when the user space reference to a map reaches zero.
 */
void bpf_timer_cancel_and_free(void *val)
{
	struct bpf_hrtimer *t;

	t = (struct bpf_hrtimer *)__bpf_async_cancel_and_free(val);

	if (!t)
		return;
	/* We check that bpf_map_delete/update_elem() was called from timer
	 * callback_fn. In such case we don't call hrtimer_cancel() (since it
	 * will deadlock) and don't call hrtimer_try_to_cancel() (since it will
	 * just return -1). Though callback_fn is still running on this cpu it's
	 * safe to do kfree(t) because bpf_timer_cb() read everything it needed
	 * from 't'. The bpf subprog callback_fn won't be able to access 't',
	 * since async->cb = NULL was already done. The timer will be
	 * effectively cancelled because bpf_timer_cb() will return
	 * HRTIMER_NORESTART.
	 *
	 * However, it is possible the timer callback_fn calling us armed the
	 * timer _before_ calling us, such that failing to cancel it here will
	 * cause it to possibly use struct hrtimer after freeing bpf_hrtimer.
	 * Therefore, we _need_ to cancel any outstanding timers before we do
	 * kfree_rcu, even though no more timers can be armed.
	 *
	 * Moreover, we need to schedule work even if timer does not belong to
	 * the calling callback_fn, as on two different CPUs, we can end up in a
	 * situation where both sides run in parallel, try to cancel one
	 * another, and we end up waiting on both sides in hrtimer_cancel
	 * without making forward progress, since timer1 depends on time2
	 * callback to finish, and vice versa.
	 *
	 *  CPU 1 (timer1_cb)			CPU 2 (timer2_cb)
	 *  bpf_timer_cancel_and_free(timer2)	bpf_timer_cancel_and_free(timer1)
	 *
	 * To avoid these issues, punt to workqueue context when we are in a
	 * timer callback.
	 */
	if (this_cpu_read(hrtimer_running)) {
		queue_work(system_unbound_wq, &t->cb.delete_work);
		return;
	}

	if (IS_ENABLED(CONFIG_PREEMPT_RT)) {
		/* If the timer is running on other CPU, also use a kworker to
		 * wait for the completion of the timer instead of trying to
		 * acquire a sleepable lock in hrtimer_cancel() to wait for its
		 * completion.
		 */
		if (hrtimer_try_to_cancel(&t->timer) >= 0)
			kfree_rcu(t, cb.rcu);
		else
			queue_work(system_unbound_wq, &t->cb.delete_work);
	} else {
		bpf_timer_delete_work(&t->cb.delete_work);
	}
}

/* This function is called by map_delete/update_elem for individual element and
 * by ops->map_release_uref when the user space reference to a map reaches zero.
 */
void bpf_wq_cancel_and_free(void *val)
{
	struct bpf_work *work;

	BTF_TYPE_EMIT(struct bpf_wq);

	work = (struct bpf_work *)__bpf_async_cancel_and_free(val);
	if (!work)
		return;
	/* Trigger cancel of the sleepable work, but *do not* wait for
	 * it to finish if it was running as we might not be in a
	 * sleepable context.
	 * kfree will be called once the work has finished.
	 */
	schedule_work(&work->delete_work);
}

BPF_CALL_2(bpf_kptr_xchg, void *, dst, void *, ptr)
{
	unsigned long *kptr = dst;

	/* This helper may be inlined by verifier. */
	return xchg(kptr, (unsigned long)ptr);
}

/* Unlike other PTR_TO_BTF_ID helpers the btf_id in bpf_kptr_xchg()
 * helper is determined dynamically by the verifier. Use BPF_PTR_POISON to
 * denote type that verifier will determine.
 */
static const struct bpf_func_proto bpf_kptr_xchg_proto = {
	.func         = bpf_kptr_xchg,
	.gpl_only     = false,
	.ret_type     = RET_PTR_TO_BTF_ID_OR_NULL,
	.ret_btf_id   = BPF_PTR_POISON,
	.arg1_type    = ARG_KPTR_XCHG_DEST,
	.arg2_type    = ARG_PTR_TO_BTF_ID_OR_NULL | OBJ_RELEASE,
	.arg2_btf_id  = BPF_PTR_POISON,
};

/* Since the upper 8 bits of dynptr->size is reserved, the
 * maximum supported size is 2^24 - 1.
 */
#define DYNPTR_MAX_SIZE	((1UL << 24) - 1)
#define DYNPTR_TYPE_SHIFT	28
#define DYNPTR_SIZE_MASK	0xFFFFFF
#define DYNPTR_RDONLY_BIT	BIT(31)

bool __bpf_dynptr_is_rdonly(const struct bpf_dynptr_kern *ptr)
{
	return ptr->size & DYNPTR_RDONLY_BIT;
}

void bpf_dynptr_set_rdonly(struct bpf_dynptr_kern *ptr)
{
	ptr->size |= DYNPTR_RDONLY_BIT;
}

static void bpf_dynptr_set_type(struct bpf_dynptr_kern *ptr, enum bpf_dynptr_type type)
{
	ptr->size |= type << DYNPTR_TYPE_SHIFT;
}

static enum bpf_dynptr_type bpf_dynptr_get_type(const struct bpf_dynptr_kern *ptr)
{
	return (ptr->size & ~(DYNPTR_RDONLY_BIT)) >> DYNPTR_TYPE_SHIFT;
}

u32 __bpf_dynptr_size(const struct bpf_dynptr_kern *ptr)
{
	return ptr->size & DYNPTR_SIZE_MASK;
}

static void bpf_dynptr_set_size(struct bpf_dynptr_kern *ptr, u32 new_size)
{
	u32 metadata = ptr->size & ~DYNPTR_SIZE_MASK;

	ptr->size = new_size | metadata;
}

int bpf_dynptr_check_size(u32 size)
{
	return size > DYNPTR_MAX_SIZE ? -E2BIG : 0;
}

void bpf_dynptr_init(struct bpf_dynptr_kern *ptr, void *data,
		     enum bpf_dynptr_type type, u32 offset, u32 size)
{
	ptr->data = data;
	ptr->offset = offset;
	ptr->size = size;
	bpf_dynptr_set_type(ptr, type);
}

void bpf_dynptr_set_null(struct bpf_dynptr_kern *ptr)
{
	memset(ptr, 0, sizeof(*ptr));
}

BPF_CALL_4(bpf_dynptr_from_mem, void *, data, u32, size, u64, flags, struct bpf_dynptr_kern *, ptr)
{
	int err;

	BTF_TYPE_EMIT(struct bpf_dynptr);

	err = bpf_dynptr_check_size(size);
	if (err)
		goto error;

	/* flags is currently unsupported */
	if (flags) {
		err = -EINVAL;
		goto error;
	}

	bpf_dynptr_init(ptr, data, BPF_DYNPTR_TYPE_LOCAL, 0, size);

	return 0;

error:
	bpf_dynptr_set_null(ptr);
	return err;
}

static const struct bpf_func_proto bpf_dynptr_from_mem_proto = {
	.func		= bpf_dynptr_from_mem,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg2_type	= ARG_CONST_SIZE_OR_ZERO,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_PTR_TO_DYNPTR | DYNPTR_TYPE_LOCAL | MEM_UNINIT | MEM_WRITE,
};

static int __bpf_dynptr_read(void *dst, u32 len, const struct bpf_dynptr_kern *src,
			     u32 offset, u64 flags)
{
	enum bpf_dynptr_type type;
	int err;

	if (!src->data || flags)
		return -EINVAL;

	err = bpf_dynptr_check_off_len(src, offset, len);
	if (err)
		return err;

	type = bpf_dynptr_get_type(src);

	switch (type) {
	case BPF_DYNPTR_TYPE_LOCAL:
	case BPF_DYNPTR_TYPE_RINGBUF:
		/* Source and destination may possibly overlap, hence use memmove to
		 * copy the data. E.g. bpf_dynptr_from_mem may create two dynptr
		 * pointing to overlapping PTR_TO_MAP_VALUE regions.
		 */
		memmove(dst, src->data + src->offset + offset, len);
		return 0;
	case BPF_DYNPTR_TYPE_SKB:
		return __bpf_skb_load_bytes(src->data, src->offset + offset, dst, len);
	case BPF_DYNPTR_TYPE_XDP:
		return __bpf_xdp_load_bytes(src->data, src->offset + offset, dst, len);
	default:
		WARN_ONCE(true, "bpf_dynptr_read: unknown dynptr type %d\n", type);
		return -EFAULT;
	}
}

BPF_CALL_5(bpf_dynptr_read, void *, dst, u32, len, const struct bpf_dynptr_kern *, src,
	   u32, offset, u64, flags)
{
	return __bpf_dynptr_read(dst, len, src, offset, flags);
}

static const struct bpf_func_proto bpf_dynptr_read_proto = {
	.func		= bpf_dynptr_read,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg2_type	= ARG_CONST_SIZE_OR_ZERO,
	.arg3_type	= ARG_PTR_TO_DYNPTR | MEM_RDONLY,
	.arg4_type	= ARG_ANYTHING,
	.arg5_type	= ARG_ANYTHING,
};

int __bpf_dynptr_write(const struct bpf_dynptr_kern *dst, u32 offset, void *src,
		       u32 len, u64 flags)
{
	enum bpf_dynptr_type type;
	int err;

	if (!dst->data || __bpf_dynptr_is_rdonly(dst))
		return -EINVAL;

	err = bpf_dynptr_check_off_len(dst, offset, len);
	if (err)
		return err;

	type = bpf_dynptr_get_type(dst);

	switch (type) {
	case BPF_DYNPTR_TYPE_LOCAL:
	case BPF_DYNPTR_TYPE_RINGBUF:
		if (flags)
			return -EINVAL;
		/* Source and destination may possibly overlap, hence use memmove to
		 * copy the data. E.g. bpf_dynptr_from_mem may create two dynptr
		 * pointing to overlapping PTR_TO_MAP_VALUE regions.
		 */
		memmove(dst->data + dst->offset + offset, src, len);
		return 0;
	case BPF_DYNPTR_TYPE_SKB:
		return __bpf_skb_store_bytes(dst->data, dst->offset + offset, src, len,
					     flags);
	case BPF_DYNPTR_TYPE_XDP:
		if (flags)
			return -EINVAL;
		return __bpf_xdp_store_bytes(dst->data, dst->offset + offset, src, len);
	default:
		WARN_ONCE(true, "bpf_dynptr_write: unknown dynptr type %d\n", type);
		return -EFAULT;
	}
}

BPF_CALL_5(bpf_dynptr_write, const struct bpf_dynptr_kern *, dst, u32, offset, void *, src,
	   u32, len, u64, flags)
{
	return __bpf_dynptr_write(dst, offset, src, len, flags);
}

static const struct bpf_func_proto bpf_dynptr_write_proto = {
	.func		= bpf_dynptr_write,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_DYNPTR | MEM_RDONLY,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_PTR_TO_MEM | MEM_RDONLY,
	.arg4_type	= ARG_CONST_SIZE_OR_ZERO,
	.arg5_type	= ARG_ANYTHING,
};

BPF_CALL_3(bpf_dynptr_data, const struct bpf_dynptr_kern *, ptr, u32, offset, u32, len)
{
	enum bpf_dynptr_type type;
	int err;

	if (!ptr->data)
		return 0;

	err = bpf_dynptr_check_off_len(ptr, offset, len);
	if (err)
		return 0;

	if (__bpf_dynptr_is_rdonly(ptr))
		return 0;

	type = bpf_dynptr_get_type(ptr);

	switch (type) {
	case BPF_DYNPTR_TYPE_LOCAL:
	case BPF_DYNPTR_TYPE_RINGBUF:
		return (unsigned long)(ptr->data + ptr->offset + offset);
	case BPF_DYNPTR_TYPE_SKB:
	case BPF_DYNPTR_TYPE_XDP:
		/* skb and xdp dynptrs should use bpf_dynptr_slice / bpf_dynptr_slice_rdwr */
		return 0;
	default:
		WARN_ONCE(true, "bpf_dynptr_data: unknown dynptr type %d\n", type);
		return 0;
	}
}

static const struct bpf_func_proto bpf_dynptr_data_proto = {
	.func		= bpf_dynptr_data,
	.gpl_only	= false,
	.ret_type	= RET_PTR_TO_DYNPTR_MEM_OR_NULL,
	.arg1_type	= ARG_PTR_TO_DYNPTR | MEM_RDONLY,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_CONST_ALLOC_SIZE_OR_ZERO,
};

const struct bpf_func_proto bpf_get_current_task_proto __weak;
const struct bpf_func_proto bpf_get_current_task_btf_proto __weak;
const struct bpf_func_proto bpf_probe_read_user_proto __weak;
const struct bpf_func_proto bpf_probe_read_user_str_proto __weak;
const struct bpf_func_proto bpf_probe_read_kernel_proto __weak;
const struct bpf_func_proto bpf_probe_read_kernel_str_proto __weak;
const struct bpf_func_proto bpf_task_pt_regs_proto __weak;
const struct bpf_func_proto bpf_perf_event_read_proto __weak;
const struct bpf_func_proto bpf_send_signal_proto __weak;
const struct bpf_func_proto bpf_send_signal_thread_proto __weak;
const struct bpf_func_proto bpf_get_task_stack_sleepable_proto __weak;
const struct bpf_func_proto bpf_get_task_stack_proto __weak;
const struct bpf_func_proto bpf_get_branch_snapshot_proto __weak;

const struct bpf_func_proto *
bpf_base_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_map_lookup_elem:
		return &bpf_map_lookup_elem_proto;
	case BPF_FUNC_map_update_elem:
		return &bpf_map_update_elem_proto;
	case BPF_FUNC_map_delete_elem:
		return &bpf_map_delete_elem_proto;
	case BPF_FUNC_map_push_elem:
		return &bpf_map_push_elem_proto;
	case BPF_FUNC_map_pop_elem:
		return &bpf_map_pop_elem_proto;
	case BPF_FUNC_map_peek_elem:
		return &bpf_map_peek_elem_proto;
	case BPF_FUNC_map_lookup_percpu_elem:
		return &bpf_map_lookup_percpu_elem_proto;
	case BPF_FUNC_get_prandom_u32:
		return &bpf_get_prandom_u32_proto;
	case BPF_FUNC_get_smp_processor_id:
		return &bpf_get_raw_smp_processor_id_proto;
	case BPF_FUNC_get_numa_node_id:
		return &bpf_get_numa_node_id_proto;
	case BPF_FUNC_tail_call:
		return &bpf_tail_call_proto;
	case BPF_FUNC_ktime_get_ns:
		return &bpf_ktime_get_ns_proto;
	case BPF_FUNC_ktime_get_boot_ns:
		return &bpf_ktime_get_boot_ns_proto;
	case BPF_FUNC_ktime_get_tai_ns:
		return &bpf_ktime_get_tai_ns_proto;
	case BPF_FUNC_ringbuf_output:
		return &bpf_ringbuf_output_proto;
	case BPF_FUNC_ringbuf_reserve:
		return &bpf_ringbuf_reserve_proto;
	case BPF_FUNC_ringbuf_submit:
		return &bpf_ringbuf_submit_proto;
	case BPF_FUNC_ringbuf_discard:
		return &bpf_ringbuf_discard_proto;
	case BPF_FUNC_ringbuf_query:
		return &bpf_ringbuf_query_proto;
	case BPF_FUNC_strncmp:
		return &bpf_strncmp_proto;
	case BPF_FUNC_strtol:
		return &bpf_strtol_proto;
	case BPF_FUNC_strtoul:
		return &bpf_strtoul_proto;
	case BPF_FUNC_get_current_pid_tgid:
		return &bpf_get_current_pid_tgid_proto;
	case BPF_FUNC_get_ns_current_pid_tgid:
		return &bpf_get_ns_current_pid_tgid_proto;
	case BPF_FUNC_get_current_uid_gid:
		return &bpf_get_current_uid_gid_proto;
	default:
		break;
	}

	if (!bpf_token_capable(prog->aux->token, CAP_BPF))
		return NULL;

	switch (func_id) {
	case BPF_FUNC_spin_lock:
		return &bpf_spin_lock_proto;
	case BPF_FUNC_spin_unlock:
		return &bpf_spin_unlock_proto;
	case BPF_FUNC_jiffies64:
		return &bpf_jiffies64_proto;
	case BPF_FUNC_per_cpu_ptr:
		return &bpf_per_cpu_ptr_proto;
	case BPF_FUNC_this_cpu_ptr:
		return &bpf_this_cpu_ptr_proto;
	case BPF_FUNC_timer_init:
		return &bpf_timer_init_proto;
	case BPF_FUNC_timer_set_callback:
		return &bpf_timer_set_callback_proto;
	case BPF_FUNC_timer_start:
		return &bpf_timer_start_proto;
	case BPF_FUNC_timer_cancel:
		return &bpf_timer_cancel_proto;
	case BPF_FUNC_kptr_xchg:
		return &bpf_kptr_xchg_proto;
	case BPF_FUNC_for_each_map_elem:
		return &bpf_for_each_map_elem_proto;
	case BPF_FUNC_loop:
		return &bpf_loop_proto;
	case BPF_FUNC_user_ringbuf_drain:
		return &bpf_user_ringbuf_drain_proto;
	case BPF_FUNC_ringbuf_reserve_dynptr:
		return &bpf_ringbuf_reserve_dynptr_proto;
	case BPF_FUNC_ringbuf_submit_dynptr:
		return &bpf_ringbuf_submit_dynptr_proto;
	case BPF_FUNC_ringbuf_discard_dynptr:
		return &bpf_ringbuf_discard_dynptr_proto;
	case BPF_FUNC_dynptr_from_mem:
		return &bpf_dynptr_from_mem_proto;
	case BPF_FUNC_dynptr_read:
		return &bpf_dynptr_read_proto;
	case BPF_FUNC_dynptr_write:
		return &bpf_dynptr_write_proto;
	case BPF_FUNC_dynptr_data:
		return &bpf_dynptr_data_proto;
#ifdef CONFIG_CGROUPS
	case BPF_FUNC_cgrp_storage_get:
		return &bpf_cgrp_storage_get_proto;
	case BPF_FUNC_cgrp_storage_delete:
		return &bpf_cgrp_storage_delete_proto;
	case BPF_FUNC_get_current_cgroup_id:
		return &bpf_get_current_cgroup_id_proto;
	case BPF_FUNC_get_current_ancestor_cgroup_id:
		return &bpf_get_current_ancestor_cgroup_id_proto;
	case BPF_FUNC_current_task_under_cgroup:
		return &bpf_current_task_under_cgroup_proto;
#endif
#ifdef CONFIG_CGROUP_NET_CLASSID
	case BPF_FUNC_get_cgroup_classid:
		return &bpf_get_cgroup_classid_curr_proto;
#endif
	case BPF_FUNC_task_storage_get:
		if (bpf_prog_check_recur(prog))
			return &bpf_task_storage_get_recur_proto;
		return &bpf_task_storage_get_proto;
	case BPF_FUNC_task_storage_delete:
		if (bpf_prog_check_recur(prog))
			return &bpf_task_storage_delete_recur_proto;
		return &bpf_task_storage_delete_proto;
	default:
		break;
	}

	if (!bpf_token_capable(prog->aux->token, CAP_PERFMON))
		return NULL;

	switch (func_id) {
	case BPF_FUNC_trace_printk:
		return bpf_get_trace_printk_proto();
	case BPF_FUNC_get_current_task:
		return &bpf_get_current_task_proto;
	case BPF_FUNC_get_current_task_btf:
		return &bpf_get_current_task_btf_proto;
	case BPF_FUNC_get_current_comm:
		return &bpf_get_current_comm_proto;
	case BPF_FUNC_probe_read_user:
		return &bpf_probe_read_user_proto;
	case BPF_FUNC_probe_read_kernel:
		return security_locked_down(LOCKDOWN_BPF_READ_KERNEL) < 0 ?
		       NULL : &bpf_probe_read_kernel_proto;
	case BPF_FUNC_probe_read_user_str:
		return &bpf_probe_read_user_str_proto;
	case BPF_FUNC_probe_read_kernel_str:
		return security_locked_down(LOCKDOWN_BPF_READ_KERNEL) < 0 ?
		       NULL : &bpf_probe_read_kernel_str_proto;
	case BPF_FUNC_copy_from_user:
		return &bpf_copy_from_user_proto;
	case BPF_FUNC_copy_from_user_task:
		return &bpf_copy_from_user_task_proto;
	case BPF_FUNC_snprintf_btf:
		return &bpf_snprintf_btf_proto;
	case BPF_FUNC_snprintf:
		return &bpf_snprintf_proto;
	case BPF_FUNC_task_pt_regs:
		return &bpf_task_pt_regs_proto;
	case BPF_FUNC_trace_vprintk:
		return bpf_get_trace_vprintk_proto();
	case BPF_FUNC_perf_event_read_value:
		return bpf_get_perf_event_read_value_proto();
	case BPF_FUNC_perf_event_read:
		return &bpf_perf_event_read_proto;
	case BPF_FUNC_send_signal:
		return &bpf_send_signal_proto;
	case BPF_FUNC_send_signal_thread:
		return &bpf_send_signal_thread_proto;
	case BPF_FUNC_get_task_stack:
		return prog->sleepable ? &bpf_get_task_stack_sleepable_proto
				       : &bpf_get_task_stack_proto;
	case BPF_FUNC_get_branch_snapshot:
		return &bpf_get_branch_snapshot_proto;
	case BPF_FUNC_find_vma:
		return &bpf_find_vma_proto;
	default:
		return NULL;
	}
}
EXPORT_SYMBOL_GPL(bpf_base_func_proto);

void bpf_list_head_free(const struct btf_field *field, void *list_head,
			struct bpf_spin_lock *spin_lock)
{
	struct list_head *head = list_head, *orig_head = list_head;

	BUILD_BUG_ON(sizeof(struct list_head) > sizeof(struct bpf_list_head));
	BUILD_BUG_ON(__alignof__(struct list_head) > __alignof__(struct bpf_list_head));

	/* Do the actual list draining outside the lock to not hold the lock for
	 * too long, and also prevent deadlocks if tracing programs end up
	 * executing on entry/exit of functions called inside the critical
	 * section, and end up doing map ops that call bpf_list_head_free for
	 * the same map value again.
	 */
	__bpf_spin_lock_irqsave(spin_lock);
	if (!head->next || list_empty(head))
		goto unlock;
	head = head->next;
unlock:
	INIT_LIST_HEAD(orig_head);
	__bpf_spin_unlock_irqrestore(spin_lock);

	while (head != orig_head) {
		void *obj = head;

		obj -= field->graph_root.node_offset;
		head = head->next;
		/* The contained type can also have resources, including a
		 * bpf_list_head which needs to be freed.
		 */
		__bpf_obj_drop_impl(obj, field->graph_root.value_rec, false);
	}
}

/* Like rbtree_postorder_for_each_entry_safe, but 'pos' and 'n' are
 * 'rb_node *', so field name of rb_node within containing struct is not
 * needed.
 *
 * Since bpf_rb_tree's node type has a corresponding struct btf_field with
 * graph_root.node_offset, it's not necessary to know field name
 * or type of node struct
 */
#define bpf_rbtree_postorder_for_each_entry_safe(pos, n, root) \
	for (pos = rb_first_postorder(root); \
	    pos && ({ n = rb_next_postorder(pos); 1; }); \
	    pos = n)

void bpf_rb_root_free(const struct btf_field *field, void *rb_root,
		      struct bpf_spin_lock *spin_lock)
{
	struct rb_root_cached orig_root, *root = rb_root;
	struct rb_node *pos, *n;
	void *obj;

	BUILD_BUG_ON(sizeof(struct rb_root_cached) > sizeof(struct bpf_rb_root));
	BUILD_BUG_ON(__alignof__(struct rb_root_cached) > __alignof__(struct bpf_rb_root));

	__bpf_spin_lock_irqsave(spin_lock);
	orig_root = *root;
	*root = RB_ROOT_CACHED;
	__bpf_spin_unlock_irqrestore(spin_lock);

	bpf_rbtree_postorder_for_each_entry_safe(pos, n, &orig_root.rb_root) {
		obj = pos;
		obj -= field->graph_root.node_offset;


		__bpf_obj_drop_impl(obj, field->graph_root.value_rec, false);
	}
}

__bpf_kfunc_start_defs();

__bpf_kfunc void *bpf_obj_new_impl(u64 local_type_id__k, void *meta__ign)
{
	struct btf_struct_meta *meta = meta__ign;
	u64 size = local_type_id__k;
	void *p;

	p = bpf_mem_alloc(&bpf_global_ma, size);
	if (!p)
		return NULL;
	if (meta)
		bpf_obj_init(meta->record, p);
	return p;
}

__bpf_kfunc void *bpf_percpu_obj_new_impl(u64 local_type_id__k, void *meta__ign)
{
	u64 size = local_type_id__k;

	/* The verifier has ensured that meta__ign must be NULL */
	return bpf_mem_alloc(&bpf_global_percpu_ma, size);
}

/* Must be called under migrate_disable(), as required by bpf_mem_free */
void __bpf_obj_drop_impl(void *p, const struct btf_record *rec, bool percpu)
{
	struct bpf_mem_alloc *ma;

	if (rec && rec->refcount_off >= 0 &&
	    !refcount_dec_and_test((refcount_t *)(p + rec->refcount_off))) {
		/* Object is refcounted and refcount_dec didn't result in 0
		 * refcount. Return without freeing the object
		 */
		return;
	}

	if (rec)
		bpf_obj_free_fields(rec, p);

	if (percpu)
		ma = &bpf_global_percpu_ma;
	else
		ma = &bpf_global_ma;
	bpf_mem_free_rcu(ma, p);
}

__bpf_kfunc void bpf_obj_drop_impl(void *p__alloc, void *meta__ign)
{
	struct btf_struct_meta *meta = meta__ign;
	void *p = p__alloc;

	__bpf_obj_drop_impl(p, meta ? meta->record : NULL, false);
}

__bpf_kfunc void bpf_percpu_obj_drop_impl(void *p__alloc, void *meta__ign)
{
	/* The verifier has ensured that meta__ign must be NULL */
	bpf_mem_free_rcu(&bpf_global_percpu_ma, p__alloc);
}

__bpf_kfunc void *bpf_refcount_acquire_impl(void *p__refcounted_kptr, void *meta__ign)
{
	struct btf_struct_meta *meta = meta__ign;
	struct bpf_refcount *ref;

	/* Could just cast directly to refcount_t *, but need some code using
	 * bpf_refcount type so that it is emitted in vmlinux BTF
	 */
	ref = (struct bpf_refcount *)(p__refcounted_kptr + meta->record->refcount_off);
	if (!refcount_inc_not_zero((refcount_t *)ref))
		return NULL;

	/* Verifier strips KF_RET_NULL if input is owned ref, see is_kfunc_ret_null
	 * in verifier.c
	 */
	return (void *)p__refcounted_kptr;
}

static int __bpf_list_add(struct bpf_list_node_kern *node,
			  struct bpf_list_head *head,
			  bool tail, struct btf_record *rec, u64 off)
{
	struct list_head *n = &node->list_head, *h = (void *)head;

	/* If list_head was 0-initialized by map, bpf_obj_init_field wasn't
	 * called on its fields, so init here
	 */
	if (unlikely(!h->next))
		INIT_LIST_HEAD(h);

	/* node->owner != NULL implies !list_empty(n), no need to separately
	 * check the latter
	 */
	if (cmpxchg(&node->owner, NULL, BPF_PTR_POISON)) {
		/* Only called from BPF prog, no need to migrate_disable */
		__bpf_obj_drop_impl((void *)n - off, rec, false);
		return -EINVAL;
	}

	tail ? list_add_tail(n, h) : list_add(n, h);
	WRITE_ONCE(node->owner, head);

	return 0;
}

__bpf_kfunc int bpf_list_push_front_impl(struct bpf_list_head *head,
					 struct bpf_list_node *node,
					 void *meta__ign, u64 off)
{
	struct bpf_list_node_kern *n = (void *)node;
	struct btf_struct_meta *meta = meta__ign;

	return __bpf_list_add(n, head, false, meta ? meta->record : NULL, off);
}

__bpf_kfunc int bpf_list_push_back_impl(struct bpf_list_head *head,
					struct bpf_list_node *node,
					void *meta__ign, u64 off)
{
	struct bpf_list_node_kern *n = (void *)node;
	struct btf_struct_meta *meta = meta__ign;

	return __bpf_list_add(n, head, true, meta ? meta->record : NULL, off);
}

static struct bpf_list_node *__bpf_list_del(struct bpf_list_head *head, bool tail)
{
	struct list_head *n, *h = (void *)head;
	struct bpf_list_node_kern *node;

	/* If list_head was 0-initialized by map, bpf_obj_init_field wasn't
	 * called on its fields, so init here
	 */
	if (unlikely(!h->next))
		INIT_LIST_HEAD(h);
	if (list_empty(h))
		return NULL;

	n = tail ? h->prev : h->next;
	node = container_of(n, struct bpf_list_node_kern, list_head);
	if (WARN_ON_ONCE(READ_ONCE(node->owner) != head))
		return NULL;

	list_del_init(n);
	WRITE_ONCE(node->owner, NULL);
	return (struct bpf_list_node *)n;
}

__bpf_kfunc struct bpf_list_node *bpf_list_pop_front(struct bpf_list_head *head)
{
	return __bpf_list_del(head, false);
}

__bpf_kfunc struct bpf_list_node *bpf_list_pop_back(struct bpf_list_head *head)
{
	return __bpf_list_del(head, true);
}

__bpf_kfunc struct bpf_list_node *bpf_list_front(struct bpf_list_head *head)
{
	struct list_head *h = (struct list_head *)head;

	if (list_empty(h) || unlikely(!h->next))
		return NULL;

	return (struct bpf_list_node *)h->next;
}

__bpf_kfunc struct bpf_list_node *bpf_list_back(struct bpf_list_head *head)
{
	struct list_head *h = (struct list_head *)head;

	if (list_empty(h) || unlikely(!h->next))
		return NULL;

	return (struct bpf_list_node *)h->prev;
}

__bpf_kfunc struct bpf_rb_node *bpf_rbtree_remove(struct bpf_rb_root *root,
						  struct bpf_rb_node *node)
{
	struct bpf_rb_node_kern *node_internal = (struct bpf_rb_node_kern *)node;
	struct rb_root_cached *r = (struct rb_root_cached *)root;
	struct rb_node *n = &node_internal->rb_node;

	/* node_internal->owner != root implies either RB_EMPTY_NODE(n) or
	 * n is owned by some other tree. No need to check RB_EMPTY_NODE(n)
	 */
	if (READ_ONCE(node_internal->owner) != root)
		return NULL;

	rb_erase_cached(n, r);
	RB_CLEAR_NODE(n);
	WRITE_ONCE(node_internal->owner, NULL);
	return (struct bpf_rb_node *)n;
}

/* Need to copy rbtree_add_cached's logic here because our 'less' is a BPF
 * program
 */
static int __bpf_rbtree_add(struct bpf_rb_root *root,
			    struct bpf_rb_node_kern *node,
			    void *less, struct btf_record *rec, u64 off)
{
	struct rb_node **link = &((struct rb_root_cached *)root)->rb_root.rb_node;
	struct rb_node *parent = NULL, *n = &node->rb_node;
	bpf_callback_t cb = (bpf_callback_t)less;
	bool leftmost = true;

	/* node->owner != NULL implies !RB_EMPTY_NODE(n), no need to separately
	 * check the latter
	 */
	if (cmpxchg(&node->owner, NULL, BPF_PTR_POISON)) {
		/* Only called from BPF prog, no need to migrate_disable */
		__bpf_obj_drop_impl((void *)n - off, rec, false);
		return -EINVAL;
	}

	while (*link) {
		parent = *link;
		if (cb((uintptr_t)node, (uintptr_t)parent, 0, 0, 0)) {
			link = &parent->rb_left;
		} else {
			link = &parent->rb_right;
			leftmost = false;
		}
	}

	rb_link_node(n, parent, link);
	rb_insert_color_cached(n, (struct rb_root_cached *)root, leftmost);
	WRITE_ONCE(node->owner, root);
	return 0;
}

__bpf_kfunc int bpf_rbtree_add_impl(struct bpf_rb_root *root, struct bpf_rb_node *node,
				    bool (less)(struct bpf_rb_node *a, const struct bpf_rb_node *b),
				    void *meta__ign, u64 off)
{
	struct btf_struct_meta *meta = meta__ign;
	struct bpf_rb_node_kern *n = (void *)node;

	return __bpf_rbtree_add(root, n, (void *)less, meta ? meta->record : NULL, off);
}

__bpf_kfunc struct bpf_rb_node *bpf_rbtree_first(struct bpf_rb_root *root)
{
	struct rb_root_cached *r = (struct rb_root_cached *)root;

	return (struct bpf_rb_node *)rb_first_cached(r);
}

__bpf_kfunc struct bpf_rb_node *bpf_rbtree_root(struct bpf_rb_root *root)
{
	struct rb_root_cached *r = (struct rb_root_cached *)root;

	return (struct bpf_rb_node *)r->rb_root.rb_node;
}

__bpf_kfunc struct bpf_rb_node *bpf_rbtree_left(struct bpf_rb_root *root, struct bpf_rb_node *node)
{
	struct bpf_rb_node_kern *node_internal = (struct bpf_rb_node_kern *)node;

	if (READ_ONCE(node_internal->owner) != root)
		return NULL;

	return (struct bpf_rb_node *)node_internal->rb_node.rb_left;
}

__bpf_kfunc struct bpf_rb_node *bpf_rbtree_right(struct bpf_rb_root *root, struct bpf_rb_node *node)
{
	struct bpf_rb_node_kern *node_internal = (struct bpf_rb_node_kern *)node;

	if (READ_ONCE(node_internal->owner) != root)
		return NULL;

	return (struct bpf_rb_node *)node_internal->rb_node.rb_right;
}

/**
 * bpf_task_acquire - Acquire a reference to a task. A task acquired by this
 * kfunc which is not stored in a map as a kptr, must be released by calling
 * bpf_task_release().
 * @p: The task on which a reference is being acquired.
 */
__bpf_kfunc struct task_struct *bpf_task_acquire(struct task_struct *p)
{
	if (refcount_inc_not_zero(&p->rcu_users))
		return p;
	return NULL;
}

/**
 * bpf_task_release - Release the reference acquired on a task.
 * @p: The task on which a reference is being released.
 */
__bpf_kfunc void bpf_task_release(struct task_struct *p)
{
	put_task_struct_rcu_user(p);
}

__bpf_kfunc void bpf_task_release_dtor(void *p)
{
	put_task_struct_rcu_user(p);
}
CFI_NOSEAL(bpf_task_release_dtor);

#ifdef CONFIG_CGROUPS
/**
 * bpf_cgroup_acquire - Acquire a reference to a cgroup. A cgroup acquired by
 * this kfunc which is not stored in a map as a kptr, must be released by
 * calling bpf_cgroup_release().
 * @cgrp: The cgroup on which a reference is being acquired.
 */
__bpf_kfunc struct cgroup *bpf_cgroup_acquire(struct cgroup *cgrp)
{
	return cgroup_tryget(cgrp) ? cgrp : NULL;
}

/**
 * bpf_cgroup_release - Release the reference acquired on a cgroup.
 * If this kfunc is invoked in an RCU read region, the cgroup is guaranteed to
 * not be freed until the current grace period has ended, even if its refcount
 * drops to 0.
 * @cgrp: The cgroup on which a reference is being released.
 */
__bpf_kfunc void bpf_cgroup_release(struct cgroup *cgrp)
{
	cgroup_put(cgrp);
}

__bpf_kfunc void bpf_cgroup_release_dtor(void *cgrp)
{
	cgroup_put(cgrp);
}
CFI_NOSEAL(bpf_cgroup_release_dtor);

/**
 * bpf_cgroup_ancestor - Perform a lookup on an entry in a cgroup's ancestor
 * array. A cgroup returned by this kfunc which is not subsequently stored in a
 * map, must be released by calling bpf_cgroup_release().
 * @cgrp: The cgroup for which we're performing a lookup.
 * @level: The level of ancestor to look up.
 */
__bpf_kfunc struct cgroup *bpf_cgroup_ancestor(struct cgroup *cgrp, int level)
{
	struct cgroup *ancestor;

	if (level > cgrp->level || level < 0)
		return NULL;

	/* cgrp's refcnt could be 0 here, but ancestors can still be accessed */
	ancestor = cgrp->ancestors[level];
	if (!cgroup_tryget(ancestor))
		return NULL;
	return ancestor;
}

/**
 * bpf_cgroup_from_id - Find a cgroup from its ID. A cgroup returned by this
 * kfunc which is not subsequently stored in a map, must be released by calling
 * bpf_cgroup_release().
 * @cgid: cgroup id.
 */
__bpf_kfunc struct cgroup *bpf_cgroup_from_id(u64 cgid)
{
	struct cgroup *cgrp;

	cgrp = cgroup_get_from_id(cgid);
	if (IS_ERR(cgrp))
		return NULL;
	return cgrp;
}

/**
 * bpf_task_under_cgroup - wrap task_under_cgroup_hierarchy() as a kfunc, test
 * task's membership of cgroup ancestry.
 * @task: the task to be tested
 * @ancestor: possible ancestor of @task's cgroup
 *
 * Tests whether @task's default cgroup hierarchy is a descendant of @ancestor.
 * It follows all the same rules as cgroup_is_descendant, and only applies
 * to the default hierarchy.
 */
__bpf_kfunc long bpf_task_under_cgroup(struct task_struct *task,
				       struct cgroup *ancestor)
{
	long ret;

	rcu_read_lock();
	ret = task_under_cgroup_hierarchy(task, ancestor);
	rcu_read_unlock();
	return ret;
}

BPF_CALL_2(bpf_current_task_under_cgroup, struct bpf_map *, map, u32, idx)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	struct cgroup *cgrp;

	if (unlikely(idx >= array->map.max_entries))
		return -E2BIG;

	cgrp = READ_ONCE(array->ptrs[idx]);
	if (unlikely(!cgrp))
		return -EAGAIN;

	return task_under_cgroup_hierarchy(current, cgrp);
}

const struct bpf_func_proto bpf_current_task_under_cgroup_proto = {
	.func           = bpf_current_task_under_cgroup,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type      = ARG_CONST_MAP_PTR,
	.arg2_type      = ARG_ANYTHING,
};

/**
 * bpf_task_get_cgroup1 - Acquires the associated cgroup of a task within a
 * specific cgroup1 hierarchy. The cgroup1 hierarchy is identified by its
 * hierarchy ID.
 * @task: The target task
 * @hierarchy_id: The ID of a cgroup1 hierarchy
 *
 * On success, the cgroup is returen. On failure, NULL is returned.
 */
__bpf_kfunc struct cgroup *
bpf_task_get_cgroup1(struct task_struct *task, int hierarchy_id)
{
	struct cgroup *cgrp = task_get_cgroup1(task, hierarchy_id);

	if (IS_ERR(cgrp))
		return NULL;
	return cgrp;
}
#endif /* CONFIG_CGROUPS */

/**
 * bpf_task_from_pid - Find a struct task_struct from its pid by looking it up
 * in the root pid namespace idr. If a task is returned, it must either be
 * stored in a map, or released with bpf_task_release().
 * @pid: The pid of the task being looked up.
 */
__bpf_kfunc struct task_struct *bpf_task_from_pid(s32 pid)
{
	struct task_struct *p;

	rcu_read_lock();
	p = find_task_by_pid_ns(pid, &init_pid_ns);
	if (p)
		p = bpf_task_acquire(p);
	rcu_read_unlock();

	return p;
}

/**
 * bpf_task_from_vpid - Find a struct task_struct from its vpid by looking it up
 * in the pid namespace of the current task. If a task is returned, it must
 * either be stored in a map, or released with bpf_task_release().
 * @vpid: The vpid of the task being looked up.
 */
__bpf_kfunc struct task_struct *bpf_task_from_vpid(s32 vpid)
{
	struct task_struct *p;

	rcu_read_lock();
	p = find_task_by_vpid(vpid);
	if (p)
		p = bpf_task_acquire(p);
	rcu_read_unlock();

	return p;
}

/**
 * bpf_dynptr_slice() - Obtain a read-only pointer to the dynptr data.
 * @p: The dynptr whose data slice to retrieve
 * @offset: Offset into the dynptr
 * @buffer__opt: User-provided buffer to copy contents into.  May be NULL
 * @buffer__szk: Size (in bytes) of the buffer if present. This is the
 *               length of the requested slice. This must be a constant.
 *
 * For non-skb and non-xdp type dynptrs, there is no difference between
 * bpf_dynptr_slice and bpf_dynptr_data.
 *
 *  If buffer__opt is NULL, the call will fail if buffer_opt was needed.
 *
 * If the intention is to write to the data slice, please use
 * bpf_dynptr_slice_rdwr.
 *
 * The user must check that the returned pointer is not null before using it.
 *
 * Please note that in the case of skb and xdp dynptrs, bpf_dynptr_slice
 * does not change the underlying packet data pointers, so a call to
 * bpf_dynptr_slice will not invalidate any ctx->data/data_end pointers in
 * the bpf program.
 *
 * Return: NULL if the call failed (eg invalid dynptr), pointer to a read-only
 * data slice (can be either direct pointer to the data or a pointer to the user
 * provided buffer, with its contents containing the data, if unable to obtain
 * direct pointer)
 */
__bpf_kfunc void *bpf_dynptr_slice(const struct bpf_dynptr *p, u32 offset,
				   void *buffer__opt, u32 buffer__szk)
{
	const struct bpf_dynptr_kern *ptr = (struct bpf_dynptr_kern *)p;
	enum bpf_dynptr_type type;
	u32 len = buffer__szk;
	int err;

	if (!ptr->data)
		return NULL;

	err = bpf_dynptr_check_off_len(ptr, offset, len);
	if (err)
		return NULL;

	type = bpf_dynptr_get_type(ptr);

	switch (type) {
	case BPF_DYNPTR_TYPE_LOCAL:
	case BPF_DYNPTR_TYPE_RINGBUF:
		return ptr->data + ptr->offset + offset;
	case BPF_DYNPTR_TYPE_SKB:
		if (buffer__opt)
			return skb_header_pointer(ptr->data, ptr->offset + offset, len, buffer__opt);
		else
			return skb_pointer_if_linear(ptr->data, ptr->offset + offset, len);
	case BPF_DYNPTR_TYPE_XDP:
	{
		void *xdp_ptr = bpf_xdp_pointer(ptr->data, ptr->offset + offset, len);
		if (!IS_ERR_OR_NULL(xdp_ptr))
			return xdp_ptr;

		if (!buffer__opt)
			return NULL;
		bpf_xdp_copy_buf(ptr->data, ptr->offset + offset, buffer__opt, len, false);
		return buffer__opt;
	}
	default:
		WARN_ONCE(true, "unknown dynptr type %d\n", type);
		return NULL;
	}
}

/**
 * bpf_dynptr_slice_rdwr() - Obtain a writable pointer to the dynptr data.
 * @p: The dynptr whose data slice to retrieve
 * @offset: Offset into the dynptr
 * @buffer__opt: User-provided buffer to copy contents into. May be NULL
 * @buffer__szk: Size (in bytes) of the buffer if present. This is the
 *               length of the requested slice. This must be a constant.
 *
 * For non-skb and non-xdp type dynptrs, there is no difference between
 * bpf_dynptr_slice and bpf_dynptr_data.
 *
 * If buffer__opt is NULL, the call will fail if buffer_opt was needed.
 *
 * The returned pointer is writable and may point to either directly the dynptr
 * data at the requested offset or to the buffer if unable to obtain a direct
 * data pointer to (example: the requested slice is to the paged area of an skb
 * packet). In the case where the returned pointer is to the buffer, the user
 * is responsible for persisting writes through calling bpf_dynptr_write(). This
 * usually looks something like this pattern:
 *
 * struct eth_hdr *eth = bpf_dynptr_slice_rdwr(&dynptr, 0, buffer, sizeof(buffer));
 * if (!eth)
 *	return TC_ACT_SHOT;
 *
 * // mutate eth header //
 *
 * if (eth == buffer)
 *	bpf_dynptr_write(&ptr, 0, buffer, sizeof(buffer), 0);
 *
 * Please note that, as in the example above, the user must check that the
 * returned pointer is not null before using it.
 *
 * Please also note that in the case of skb and xdp dynptrs, bpf_dynptr_slice_rdwr
 * does not change the underlying packet data pointers, so a call to
 * bpf_dynptr_slice_rdwr will not invalidate any ctx->data/data_end pointers in
 * the bpf program.
 *
 * Return: NULL if the call failed (eg invalid dynptr), pointer to a
 * data slice (can be either direct pointer to the data or a pointer to the user
 * provided buffer, with its contents containing the data, if unable to obtain
 * direct pointer)
 */
__bpf_kfunc void *bpf_dynptr_slice_rdwr(const struct bpf_dynptr *p, u32 offset,
					void *buffer__opt, u32 buffer__szk)
{
	const struct bpf_dynptr_kern *ptr = (struct bpf_dynptr_kern *)p;

	if (!ptr->data || __bpf_dynptr_is_rdonly(ptr))
		return NULL;

	/* bpf_dynptr_slice_rdwr is the same logic as bpf_dynptr_slice.
	 *
	 * For skb-type dynptrs, it is safe to write into the returned pointer
	 * if the bpf program allows skb data writes. There are two possibilities
	 * that may occur when calling bpf_dynptr_slice_rdwr:
	 *
	 * 1) The requested slice is in the head of the skb. In this case, the
	 * returned pointer is directly to skb data, and if the skb is cloned, the
	 * verifier will have uncloned it (see bpf_unclone_prologue()) already.
	 * The pointer can be directly written into.
	 *
	 * 2) Some portion of the requested slice is in the paged buffer area.
	 * In this case, the requested data will be copied out into the buffer
	 * and the returned pointer will be a pointer to the buffer. The skb
	 * will not be pulled. To persist the write, the user will need to call
	 * bpf_dynptr_write(), which will pull the skb and commit the write.
	 *
	 * Similarly for xdp programs, if the requested slice is not across xdp
	 * fragments, then a direct pointer will be returned, otherwise the data
	 * will be copied out into the buffer and the user will need to call
	 * bpf_dynptr_write() to commit changes.
	 */
	return bpf_dynptr_slice(p, offset, buffer__opt, buffer__szk);
}

__bpf_kfunc int bpf_dynptr_adjust(const struct bpf_dynptr *p, u32 start, u32 end)
{
	struct bpf_dynptr_kern *ptr = (struct bpf_dynptr_kern *)p;
	u32 size;

	if (!ptr->data || start > end)
		return -EINVAL;

	size = __bpf_dynptr_size(ptr);

	if (start > size || end > size)
		return -ERANGE;

	ptr->offset += start;
	bpf_dynptr_set_size(ptr, end - start);

	return 0;
}

__bpf_kfunc bool bpf_dynptr_is_null(const struct bpf_dynptr *p)
{
	struct bpf_dynptr_kern *ptr = (struct bpf_dynptr_kern *)p;

	return !ptr->data;
}

__bpf_kfunc bool bpf_dynptr_is_rdonly(const struct bpf_dynptr *p)
{
	struct bpf_dynptr_kern *ptr = (struct bpf_dynptr_kern *)p;

	if (!ptr->data)
		return false;

	return __bpf_dynptr_is_rdonly(ptr);
}

__bpf_kfunc __u32 bpf_dynptr_size(const struct bpf_dynptr *p)
{
	struct bpf_dynptr_kern *ptr = (struct bpf_dynptr_kern *)p;

	if (!ptr->data)
		return -EINVAL;

	return __bpf_dynptr_size(ptr);
}

__bpf_kfunc int bpf_dynptr_clone(const struct bpf_dynptr *p,
				 struct bpf_dynptr *clone__uninit)
{
	struct bpf_dynptr_kern *clone = (struct bpf_dynptr_kern *)clone__uninit;
	struct bpf_dynptr_kern *ptr = (struct bpf_dynptr_kern *)p;

	if (!ptr->data) {
		bpf_dynptr_set_null(clone);
		return -EINVAL;
	}

	*clone = *ptr;

	return 0;
}

/**
 * bpf_dynptr_copy() - Copy data from one dynptr to another.
 * @dst_ptr: Destination dynptr - where data should be copied to
 * @dst_off: Offset into the destination dynptr
 * @src_ptr: Source dynptr - where data should be copied from
 * @src_off: Offset into the source dynptr
 * @size: Length of the data to copy from source to destination
 *
 * Copies data from source dynptr to destination dynptr.
 * Returns 0 on success; negative error, otherwise.
 */
__bpf_kfunc int bpf_dynptr_copy(struct bpf_dynptr *dst_ptr, u32 dst_off,
				struct bpf_dynptr *src_ptr, u32 src_off, u32 size)
{
	struct bpf_dynptr_kern *dst = (struct bpf_dynptr_kern *)dst_ptr;
	struct bpf_dynptr_kern *src = (struct bpf_dynptr_kern *)src_ptr;
	void *src_slice, *dst_slice;
	char buf[256];
	u32 off;

	src_slice = bpf_dynptr_slice(src_ptr, src_off, NULL, size);
	dst_slice = bpf_dynptr_slice_rdwr(dst_ptr, dst_off, NULL, size);

	if (src_slice && dst_slice) {
		memmove(dst_slice, src_slice, size);
		return 0;
	}

	if (src_slice)
		return __bpf_dynptr_write(dst, dst_off, src_slice, size, 0);

	if (dst_slice)
		return __bpf_dynptr_read(dst_slice, size, src, src_off, 0);

	if (bpf_dynptr_check_off_len(dst, dst_off, size) ||
	    bpf_dynptr_check_off_len(src, src_off, size))
		return -E2BIG;

	off = 0;
	while (off < size) {
		u32 chunk_sz = min_t(u32, sizeof(buf), size - off);
		int err;

		err = __bpf_dynptr_read(buf, chunk_sz, src, src_off + off, 0);
		if (err)
			return err;
		err = __bpf_dynptr_write(dst, dst_off + off, buf, chunk_sz, 0);
		if (err)
			return err;

		off += chunk_sz;
	}
	return 0;
}

__bpf_kfunc void *bpf_cast_to_kern_ctx(void *obj)
{
	return obj;
}

__bpf_kfunc void *bpf_rdonly_cast(const void *obj__ign, u32 btf_id__k)
{
	return (void *)obj__ign;
}

__bpf_kfunc void bpf_rcu_read_lock(void)
{
	rcu_read_lock();
}

__bpf_kfunc void bpf_rcu_read_unlock(void)
{
	rcu_read_unlock();
}

struct bpf_throw_ctx {
	struct bpf_prog_aux *aux;
	u64 sp;
	u64 bp;
	int cnt;
};

static bool bpf_stack_walker(void *cookie, u64 ip, u64 sp, u64 bp)
{
	struct bpf_throw_ctx *ctx = cookie;
	struct bpf_prog *prog;

	if (!is_bpf_text_address(ip))
		return !ctx->cnt;
	prog = bpf_prog_ksym_find(ip);
	ctx->cnt++;
	if (bpf_is_subprog(prog))
		return true;
	ctx->aux = prog->aux;
	ctx->sp = sp;
	ctx->bp = bp;
	return false;
}

__bpf_kfunc void bpf_throw(u64 cookie)
{
	struct bpf_throw_ctx ctx = {};

	arch_bpf_stack_walk(bpf_stack_walker, &ctx);
	WARN_ON_ONCE(!ctx.aux);
	if (ctx.aux)
		WARN_ON_ONCE(!ctx.aux->exception_boundary);
	WARN_ON_ONCE(!ctx.bp);
	WARN_ON_ONCE(!ctx.cnt);
	/* Prevent KASAN false positives for CONFIG_KASAN_STACK by unpoisoning
	 * deeper stack depths than ctx.sp as we do not return from bpf_throw,
	 * which skips compiler generated instrumentation to do the same.
	 */
	kasan_unpoison_task_stack_below((void *)(long)ctx.sp);
	ctx.aux->bpf_exception_cb(cookie, ctx.sp, ctx.bp, 0, 0);
	WARN(1, "A call to BPF exception callback should never return\n");
}

__bpf_kfunc int bpf_wq_init(struct bpf_wq *wq, void *p__map, unsigned int flags)
{
	struct bpf_async_kern *async = (struct bpf_async_kern *)wq;
	struct bpf_map *map = p__map;

	BUILD_BUG_ON(sizeof(struct bpf_async_kern) > sizeof(struct bpf_wq));
	BUILD_BUG_ON(__alignof__(struct bpf_async_kern) != __alignof__(struct bpf_wq));

	if (flags)
		return -EINVAL;

	return __bpf_async_init(async, map, flags, BPF_ASYNC_TYPE_WQ);
}

__bpf_kfunc int bpf_wq_start(struct bpf_wq *wq, unsigned int flags)
{
	struct bpf_async_kern *async = (struct bpf_async_kern *)wq;
	struct bpf_work *w;

	if (in_nmi())
		return -EOPNOTSUPP;
	if (flags)
		return -EINVAL;
	w = READ_ONCE(async->work);
	if (!w || !READ_ONCE(w->cb.prog))
		return -EINVAL;

	schedule_work(&w->work);
	return 0;
}

__bpf_kfunc int bpf_wq_set_callback_impl(struct bpf_wq *wq,
					 int (callback_fn)(void *map, int *key, void *value),
					 unsigned int flags,
					 void *aux__prog)
{
	struct bpf_prog_aux *aux = (struct bpf_prog_aux *)aux__prog;
	struct bpf_async_kern *async = (struct bpf_async_kern *)wq;

	if (flags)
		return -EINVAL;

	return __bpf_async_set_callback(async, callback_fn, aux, flags, BPF_ASYNC_TYPE_WQ);
}

__bpf_kfunc void bpf_preempt_disable(void)
{
	preempt_disable();
}

__bpf_kfunc void bpf_preempt_enable(void)
{
	preempt_enable();
}

struct bpf_iter_bits {
	__u64 __opaque[2];
} __aligned(8);

#define BITS_ITER_NR_WORDS_MAX 511

struct bpf_iter_bits_kern {
	union {
		__u64 *bits;
		__u64 bits_copy;
	};
	int nr_bits;
	int bit;
} __aligned(8);

/* On 64-bit hosts, unsigned long and u64 have the same size, so passing
 * a u64 pointer and an unsigned long pointer to find_next_bit() will
 * return the same result, as both point to the same 8-byte area.
 *
 * For 32-bit little-endian hosts, using a u64 pointer or unsigned long
 * pointer also makes no difference. This is because the first iterated
 * unsigned long is composed of bits 0-31 of the u64 and the second unsigned
 * long is composed of bits 32-63 of the u64.
 *
 * However, for 32-bit big-endian hosts, this is not the case. The first
 * iterated unsigned long will be bits 32-63 of the u64, so swap these two
 * ulong values within the u64.
 */
static void swap_ulong_in_u64(u64 *bits, unsigned int nr)
{
#if (BITS_PER_LONG == 32) && defined(__BIG_ENDIAN)
	unsigned int i;

	for (i = 0; i < nr; i++)
		bits[i] = (bits[i] >> 32) | ((u64)(u32)bits[i] << 32);
#endif
}

/**
 * bpf_iter_bits_new() - Initialize a new bits iterator for a given memory area
 * @it: The new bpf_iter_bits to be created
 * @unsafe_ptr__ign: A pointer pointing to a memory area to be iterated over
 * @nr_words: The size of the specified memory area, measured in 8-byte units.
 * The maximum value of @nr_words is @BITS_ITER_NR_WORDS_MAX. This limit may be
 * further reduced by the BPF memory allocator implementation.
 *
 * This function initializes a new bpf_iter_bits structure for iterating over
 * a memory area which is specified by the @unsafe_ptr__ign and @nr_words. It
 * copies the data of the memory area to the newly created bpf_iter_bits @it for
 * subsequent iteration operations.
 *
 * On success, 0 is returned. On failure, ERR is returned.
 */
__bpf_kfunc int
bpf_iter_bits_new(struct bpf_iter_bits *it, const u64 *unsafe_ptr__ign, u32 nr_words)
{
	struct bpf_iter_bits_kern *kit = (void *)it;
	u32 nr_bytes = nr_words * sizeof(u64);
	u32 nr_bits = BYTES_TO_BITS(nr_bytes);
	int err;

	BUILD_BUG_ON(sizeof(struct bpf_iter_bits_kern) != sizeof(struct bpf_iter_bits));
	BUILD_BUG_ON(__alignof__(struct bpf_iter_bits_kern) !=
		     __alignof__(struct bpf_iter_bits));

	kit->nr_bits = 0;
	kit->bits_copy = 0;
	kit->bit = -1;

	if (!unsafe_ptr__ign || !nr_words)
		return -EINVAL;
	if (nr_words > BITS_ITER_NR_WORDS_MAX)
		return -E2BIG;

	/* Optimization for u64 mask */
	if (nr_bits == 64) {
		err = bpf_probe_read_kernel_common(&kit->bits_copy, nr_bytes, unsafe_ptr__ign);
		if (err)
			return -EFAULT;

		swap_ulong_in_u64(&kit->bits_copy, nr_words);

		kit->nr_bits = nr_bits;
		return 0;
	}

	if (bpf_mem_alloc_check_size(false, nr_bytes))
		return -E2BIG;

	/* Fallback to memalloc */
	kit->bits = bpf_mem_alloc(&bpf_global_ma, nr_bytes);
	if (!kit->bits)
		return -ENOMEM;

	err = bpf_probe_read_kernel_common(kit->bits, nr_bytes, unsafe_ptr__ign);
	if (err) {
		bpf_mem_free(&bpf_global_ma, kit->bits);
		return err;
	}

	swap_ulong_in_u64(kit->bits, nr_words);

	kit->nr_bits = nr_bits;
	return 0;
}

/**
 * bpf_iter_bits_next() - Get the next bit in a bpf_iter_bits
 * @it: The bpf_iter_bits to be checked
 *
 * This function returns a pointer to a number representing the value of the
 * next bit in the bits.
 *
 * If there are no further bits available, it returns NULL.
 */
__bpf_kfunc int *bpf_iter_bits_next(struct bpf_iter_bits *it)
{
	struct bpf_iter_bits_kern *kit = (void *)it;
	int bit = kit->bit, nr_bits = kit->nr_bits;
	const void *bits;

	if (!nr_bits || bit >= nr_bits)
		return NULL;

	bits = nr_bits == 64 ? &kit->bits_copy : kit->bits;
	bit = find_next_bit(bits, nr_bits, bit + 1);
	if (bit >= nr_bits) {
		kit->bit = bit;
		return NULL;
	}

	kit->bit = bit;
	return &kit->bit;
}

/**
 * bpf_iter_bits_destroy() - Destroy a bpf_iter_bits
 * @it: The bpf_iter_bits to be destroyed
 *
 * Destroy the resource associated with the bpf_iter_bits.
 */
__bpf_kfunc void bpf_iter_bits_destroy(struct bpf_iter_bits *it)
{
	struct bpf_iter_bits_kern *kit = (void *)it;

	if (kit->nr_bits <= 64)
		return;
	bpf_mem_free(&bpf_global_ma, kit->bits);
}

/**
 * bpf_copy_from_user_str() - Copy a string from an unsafe user address
 * @dst:             Destination address, in kernel space.  This buffer must be
 *                   at least @dst__sz bytes long.
 * @dst__sz:         Maximum number of bytes to copy, includes the trailing NUL.
 * @unsafe_ptr__ign: Source address, in user space.
 * @flags:           The only supported flag is BPF_F_PAD_ZEROS
 *
 * Copies a NUL-terminated string from userspace to BPF space. If user string is
 * too long this will still ensure zero termination in the dst buffer unless
 * buffer size is 0.
 *
 * If BPF_F_PAD_ZEROS flag is set, memset the tail of @dst to 0 on success and
 * memset all of @dst on failure.
 */
__bpf_kfunc int bpf_copy_from_user_str(void *dst, u32 dst__sz, const void __user *unsafe_ptr__ign, u64 flags)
{
	int ret;

	if (unlikely(flags & ~BPF_F_PAD_ZEROS))
		return -EINVAL;

	if (unlikely(!dst__sz))
		return 0;

	ret = strncpy_from_user(dst, unsafe_ptr__ign, dst__sz - 1);
	if (ret < 0) {
		if (flags & BPF_F_PAD_ZEROS)
			memset((char *)dst, 0, dst__sz);

		return ret;
	}

	if (flags & BPF_F_PAD_ZEROS)
		memset((char *)dst + ret, 0, dst__sz - ret);
	else
		((char *)dst)[ret] = '\0';

	return ret + 1;
}

/**
 * bpf_copy_from_user_task_str() - Copy a string from an task's address space
 * @dst:             Destination address, in kernel space.  This buffer must be
 *                   at least @dst__sz bytes long.
 * @dst__sz:         Maximum number of bytes to copy, includes the trailing NUL.
 * @unsafe_ptr__ign: Source address in the task's address space.
 * @tsk:             The task whose address space will be used
 * @flags:           The only supported flag is BPF_F_PAD_ZEROS
 *
 * Copies a NUL terminated string from a task's address space to @dst__sz
 * buffer. If user string is too long this will still ensure zero termination
 * in the @dst__sz buffer unless buffer size is 0.
 *
 * If BPF_F_PAD_ZEROS flag is set, memset the tail of @dst__sz to 0 on success
 * and memset all of @dst__sz on failure.
 *
 * Return: The number of copied bytes on success including the NUL terminator.
 * A negative error code on failure.
 */
__bpf_kfunc int bpf_copy_from_user_task_str(void *dst, u32 dst__sz,
					    const void __user *unsafe_ptr__ign,
					    struct task_struct *tsk, u64 flags)
{
	int ret;

	if (unlikely(flags & ~BPF_F_PAD_ZEROS))
		return -EINVAL;

	if (unlikely(dst__sz == 0))
		return 0;

	ret = copy_remote_vm_str(tsk, (unsigned long)unsafe_ptr__ign, dst, dst__sz, 0);
	if (ret < 0) {
		if (flags & BPF_F_PAD_ZEROS)
			memset(dst, 0, dst__sz);
		return ret;
	}

	if (flags & BPF_F_PAD_ZEROS)
		memset(dst + ret, 0, dst__sz - ret);

	return ret + 1;
}

/* Keep unsinged long in prototype so that kfunc is usable when emitted to
 * vmlinux.h in BPF programs directly, but note that while in BPF prog, the
 * unsigned long always points to 8-byte region on stack, the kernel may only
 * read and write the 4-bytes on 32-bit.
 */
__bpf_kfunc void bpf_local_irq_save(unsigned long *flags__irq_flag)
{
	local_irq_save(*flags__irq_flag);
}

__bpf_kfunc void bpf_local_irq_restore(unsigned long *flags__irq_flag)
{
	local_irq_restore(*flags__irq_flag);
}

__bpf_kfunc void __bpf_trap(void)
{
}

__bpf_kfunc_end_defs();

BTF_KFUNCS_START(generic_btf_ids)
#ifdef CONFIG_CRASH_DUMP
BTF_ID_FLAGS(func, crash_kexec, KF_DESTRUCTIVE)
#endif
BTF_ID_FLAGS(func, bpf_obj_new_impl, KF_ACQUIRE | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_percpu_obj_new_impl, KF_ACQUIRE | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_obj_drop_impl, KF_RELEASE)
BTF_ID_FLAGS(func, bpf_percpu_obj_drop_impl, KF_RELEASE)
BTF_ID_FLAGS(func, bpf_refcount_acquire_impl, KF_ACQUIRE | KF_RET_NULL | KF_RCU)
BTF_ID_FLAGS(func, bpf_list_push_front_impl)
BTF_ID_FLAGS(func, bpf_list_push_back_impl)
BTF_ID_FLAGS(func, bpf_list_pop_front, KF_ACQUIRE | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_list_pop_back, KF_ACQUIRE | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_list_front, KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_list_back, KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_task_acquire, KF_ACQUIRE | KF_RCU | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_task_release, KF_RELEASE)
BTF_ID_FLAGS(func, bpf_rbtree_remove, KF_ACQUIRE | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_rbtree_add_impl)
BTF_ID_FLAGS(func, bpf_rbtree_first, KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_rbtree_root, KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_rbtree_left, KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_rbtree_right, KF_RET_NULL)

#ifdef CONFIG_CGROUPS
BTF_ID_FLAGS(func, bpf_cgroup_acquire, KF_ACQUIRE | KF_RCU | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_cgroup_release, KF_RELEASE)
BTF_ID_FLAGS(func, bpf_cgroup_ancestor, KF_ACQUIRE | KF_RCU | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_cgroup_from_id, KF_ACQUIRE | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_task_under_cgroup, KF_RCU)
BTF_ID_FLAGS(func, bpf_task_get_cgroup1, KF_ACQUIRE | KF_RCU | KF_RET_NULL)
#endif
BTF_ID_FLAGS(func, bpf_task_from_pid, KF_ACQUIRE | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_task_from_vpid, KF_ACQUIRE | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_throw)
#ifdef CONFIG_BPF_EVENTS
BTF_ID_FLAGS(func, bpf_send_signal_task, KF_TRUSTED_ARGS)
#endif
BTF_KFUNCS_END(generic_btf_ids)

static const struct btf_kfunc_id_set generic_kfunc_set = {
	.owner = THIS_MODULE,
	.set   = &generic_btf_ids,
};


BTF_ID_LIST(generic_dtor_ids)
BTF_ID(struct, task_struct)
BTF_ID(func, bpf_task_release_dtor)
#ifdef CONFIG_CGROUPS
BTF_ID(struct, cgroup)
BTF_ID(func, bpf_cgroup_release_dtor)
#endif

BTF_KFUNCS_START(common_btf_ids)
BTF_ID_FLAGS(func, bpf_cast_to_kern_ctx, KF_FASTCALL)
BTF_ID_FLAGS(func, bpf_rdonly_cast, KF_FASTCALL)
BTF_ID_FLAGS(func, bpf_rcu_read_lock)
BTF_ID_FLAGS(func, bpf_rcu_read_unlock)
BTF_ID_FLAGS(func, bpf_dynptr_slice, KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_dynptr_slice_rdwr, KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_iter_num_new, KF_ITER_NEW)
BTF_ID_FLAGS(func, bpf_iter_num_next, KF_ITER_NEXT | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_iter_num_destroy, KF_ITER_DESTROY)
BTF_ID_FLAGS(func, bpf_iter_task_vma_new, KF_ITER_NEW | KF_RCU)
BTF_ID_FLAGS(func, bpf_iter_task_vma_next, KF_ITER_NEXT | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_iter_task_vma_destroy, KF_ITER_DESTROY)
#ifdef CONFIG_CGROUPS
BTF_ID_FLAGS(func, bpf_iter_css_task_new, KF_ITER_NEW | KF_TRUSTED_ARGS)
BTF_ID_FLAGS(func, bpf_iter_css_task_next, KF_ITER_NEXT | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_iter_css_task_destroy, KF_ITER_DESTROY)
BTF_ID_FLAGS(func, bpf_iter_css_new, KF_ITER_NEW | KF_TRUSTED_ARGS | KF_RCU_PROTECTED)
BTF_ID_FLAGS(func, bpf_iter_css_next, KF_ITER_NEXT | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_iter_css_destroy, KF_ITER_DESTROY)
#endif
BTF_ID_FLAGS(func, bpf_iter_task_new, KF_ITER_NEW | KF_TRUSTED_ARGS | KF_RCU_PROTECTED)
BTF_ID_FLAGS(func, bpf_iter_task_next, KF_ITER_NEXT | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_iter_task_destroy, KF_ITER_DESTROY)
BTF_ID_FLAGS(func, bpf_dynptr_adjust)
BTF_ID_FLAGS(func, bpf_dynptr_is_null)
BTF_ID_FLAGS(func, bpf_dynptr_is_rdonly)
BTF_ID_FLAGS(func, bpf_dynptr_size)
BTF_ID_FLAGS(func, bpf_dynptr_clone)
BTF_ID_FLAGS(func, bpf_dynptr_copy)
#ifdef CONFIG_NET
BTF_ID_FLAGS(func, bpf_modify_return_test_tp)
#endif
BTF_ID_FLAGS(func, bpf_wq_init)
BTF_ID_FLAGS(func, bpf_wq_set_callback_impl)
BTF_ID_FLAGS(func, bpf_wq_start)
BTF_ID_FLAGS(func, bpf_preempt_disable)
BTF_ID_FLAGS(func, bpf_preempt_enable)
BTF_ID_FLAGS(func, bpf_iter_bits_new, KF_ITER_NEW)
BTF_ID_FLAGS(func, bpf_iter_bits_next, KF_ITER_NEXT | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_iter_bits_destroy, KF_ITER_DESTROY)
BTF_ID_FLAGS(func, bpf_copy_from_user_str, KF_SLEEPABLE)
BTF_ID_FLAGS(func, bpf_copy_from_user_task_str, KF_SLEEPABLE)
BTF_ID_FLAGS(func, bpf_get_kmem_cache)
BTF_ID_FLAGS(func, bpf_iter_kmem_cache_new, KF_ITER_NEW | KF_SLEEPABLE)
BTF_ID_FLAGS(func, bpf_iter_kmem_cache_next, KF_ITER_NEXT | KF_RET_NULL | KF_SLEEPABLE)
BTF_ID_FLAGS(func, bpf_iter_kmem_cache_destroy, KF_ITER_DESTROY | KF_SLEEPABLE)
BTF_ID_FLAGS(func, bpf_local_irq_save)
BTF_ID_FLAGS(func, bpf_local_irq_restore)
BTF_ID_FLAGS(func, bpf_probe_read_user_dynptr)
BTF_ID_FLAGS(func, bpf_probe_read_kernel_dynptr)
BTF_ID_FLAGS(func, bpf_probe_read_user_str_dynptr)
BTF_ID_FLAGS(func, bpf_probe_read_kernel_str_dynptr)
BTF_ID_FLAGS(func, bpf_copy_from_user_dynptr, KF_SLEEPABLE)
BTF_ID_FLAGS(func, bpf_copy_from_user_str_dynptr, KF_SLEEPABLE)
BTF_ID_FLAGS(func, bpf_copy_from_user_task_dynptr, KF_SLEEPABLE | KF_TRUSTED_ARGS)
BTF_ID_FLAGS(func, bpf_copy_from_user_task_str_dynptr, KF_SLEEPABLE | KF_TRUSTED_ARGS)
#ifdef CONFIG_DMA_SHARED_BUFFER
BTF_ID_FLAGS(func, bpf_iter_dmabuf_new, KF_ITER_NEW | KF_SLEEPABLE)
BTF_ID_FLAGS(func, bpf_iter_dmabuf_next, KF_ITER_NEXT | KF_RET_NULL | KF_SLEEPABLE)
BTF_ID_FLAGS(func, bpf_iter_dmabuf_destroy, KF_ITER_DESTROY | KF_SLEEPABLE)
#endif
BTF_ID_FLAGS(func, __bpf_trap)
#ifdef CONFIG_CGROUPS
BTF_ID_FLAGS(func, bpf_cgroup_read_xattr, KF_RCU)
#endif
BTF_KFUNCS_END(common_btf_ids)

static const struct btf_kfunc_id_set common_kfunc_set = {
	.owner = THIS_MODULE,
	.set   = &common_btf_ids,
};

static int __init kfunc_init(void)
{
	int ret;
	const struct btf_id_dtor_kfunc generic_dtors[] = {
		{
			.btf_id       = generic_dtor_ids[0],
			.kfunc_btf_id = generic_dtor_ids[1]
		},
#ifdef CONFIG_CGROUPS
		{
			.btf_id       = generic_dtor_ids[2],
			.kfunc_btf_id = generic_dtor_ids[3]
		},
#endif
	};

	ret = register_btf_kfunc_id_set(BPF_PROG_TYPE_TRACING, &generic_kfunc_set);
	ret = ret ?: register_btf_kfunc_id_set(BPF_PROG_TYPE_SCHED_CLS, &generic_kfunc_set);
	ret = ret ?: register_btf_kfunc_id_set(BPF_PROG_TYPE_XDP, &generic_kfunc_set);
	ret = ret ?: register_btf_kfunc_id_set(BPF_PROG_TYPE_STRUCT_OPS, &generic_kfunc_set);
	ret = ret ?: register_btf_kfunc_id_set(BPF_PROG_TYPE_SYSCALL, &generic_kfunc_set);
	ret = ret ?: register_btf_kfunc_id_set(BPF_PROG_TYPE_CGROUP_SKB, &generic_kfunc_set);
	ret = ret ?: register_btf_id_dtor_kfuncs(generic_dtors,
						  ARRAY_SIZE(generic_dtors),
						  THIS_MODULE);
	return ret ?: register_btf_kfunc_id_set(BPF_PROG_TYPE_UNSPEC, &common_kfunc_set);
}

late_initcall(kfunc_init);

/* Get a pointer to dynptr data up to len bytes for read only access. If
 * the dynptr doesn't have continuous data up to len bytes, return NULL.
 */
const void *__bpf_dynptr_data(const struct bpf_dynptr_kern *ptr, u32 len)
{
	const struct bpf_dynptr *p = (struct bpf_dynptr *)ptr;

	return bpf_dynptr_slice(p, 0, NULL, len);
}

/* Get a pointer to dynptr data up to len bytes for read write access. If
 * the dynptr doesn't have continuous data up to len bytes, or the dynptr
 * is read only, return NULL.
 */
void *__bpf_dynptr_data_rw(const struct bpf_dynptr_kern *ptr, u32 len)
{
	if (__bpf_dynptr_is_rdonly(ptr))
		return NULL;
	return (void *)__bpf_dynptr_data(ptr, len);
}
