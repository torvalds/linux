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

#include "../../lib/kstrtox.h"

/* If kernel subsystem is allowing eBPF programs to call this function,
 * inside its own verifier_ops->get_func_proto() callback it should return
 * bpf_map_lookup_elem_proto, so that verifier can properly check the arguments
 *
 * Different map implementations will rely on rcu in map methods
 * lookup/update/delete, therefore eBPF programs must run under rcu lock
 * if program is allowed to access maps, so check rcu_read_lock_held in
 * all three functions.
 */
BPF_CALL_2(bpf_map_lookup_elem, struct bpf_map *, map, void *, key)
{
	WARN_ON_ONCE(!rcu_read_lock_held() && !rcu_read_lock_bh_held());
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
	WARN_ON_ONCE(!rcu_read_lock_held() && !rcu_read_lock_bh_held());
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
	WARN_ON_ONCE(!rcu_read_lock_held() && !rcu_read_lock_bh_held());
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
	.arg2_type	= ARG_PTR_TO_MAP_VALUE | MEM_UNINIT,
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
	.arg2_type	= ARG_PTR_TO_MAP_VALUE | MEM_UNINIT,
};

BPF_CALL_3(bpf_map_lookup_percpu_elem, struct bpf_map *, map, void *, key, u32, cpu)
{
	WARN_ON_ONCE(!rcu_read_lock_held() && !rcu_read_lock_bh_held());
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

notrace BPF_CALL_1(bpf_spin_lock, struct bpf_spin_lock *, lock)
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

notrace BPF_CALL_1(bpf_spin_unlock, struct bpf_spin_lock *, lock)
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
	   long *, res)
{
	long long _res;
	int err;

	err = __bpf_strtoll(buf, buf_len, flags, &_res);
	if (err < 0)
		return err;
	if (_res != (long)_res)
		return -ERANGE;
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
	.arg4_type	= ARG_PTR_TO_LONG,
};

BPF_CALL_4(bpf_strtoul, const char *, buf, size_t, buf_len, u64, flags,
	   unsigned long *, res)
{
	unsigned long long _res;
	bool is_negative;
	int err;

	err = __bpf_strtoull(buf, buf_len, flags, &_res, &is_negative);
	if (err < 0)
		return err;
	if (is_negative)
		return -EINVAL;
	if (_res != (unsigned long)_res)
		return -ERANGE;
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
	.arg4_type	= ARG_PTR_TO_LONG,
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

	return (unsigned long)per_cpu_ptr((const void __percpu *)ptr, cpu);
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
	return (unsigned long)this_cpu_ptr((const void __percpu *)percpu_ptr);
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

			if ((fmt[i + 1] == 'k' || fmt[i + 1] == 'u') &&
			    fmt[i + 2] == 's') {
				fmt_ptype = fmt[i + 1];
				i += 2;
				goto fmt_str;
			}

			if (fmt[i + 1] == 0 || isspace(fmt[i + 1]) ||
			    ispunct(fmt[i + 1]) || fmt[i + 1] == 'K' ||
			    fmt[i + 1] == 'x' || fmt[i + 1] == 's' ||
			    fmt[i + 1] == 'S') {
				/* just kernel pointers */
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

/* BPF map elements can contain 'struct bpf_timer'.
 * Such map owns all of its BPF timers.
 * 'struct bpf_timer' is allocated as part of map element allocation
 * and it's zero initialized.
 * That space is used to keep 'struct bpf_timer_kern'.
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
	struct hrtimer timer;
	struct bpf_map *map;
	struct bpf_prog *prog;
	void __rcu *callback_fn;
	void *value;
};

/* the actual struct hidden inside uapi struct bpf_timer */
struct bpf_timer_kern {
	struct bpf_hrtimer *timer;
	/* bpf_spin_lock is used here instead of spinlock_t to make
	 * sure that it always fits into space reserved by struct bpf_timer
	 * regardless of LOCKDEP and spinlock debug flags.
	 */
	struct bpf_spin_lock lock;
} __attribute__((aligned(8)));

static DEFINE_PER_CPU(struct bpf_hrtimer *, hrtimer_running);

static enum hrtimer_restart bpf_timer_cb(struct hrtimer *hrtimer)
{
	struct bpf_hrtimer *t = container_of(hrtimer, struct bpf_hrtimer, timer);
	struct bpf_map *map = t->map;
	void *value = t->value;
	bpf_callback_t callback_fn;
	void *key;
	u32 idx;

	BTF_TYPE_EMIT(struct bpf_timer);
	callback_fn = rcu_dereference_check(t->callback_fn, rcu_read_lock_bh_held());
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

BPF_CALL_3(bpf_timer_init, struct bpf_timer_kern *, timer, struct bpf_map *, map,
	   u64, flags)
{
	clockid_t clockid = flags & (MAX_CLOCKS - 1);
	struct bpf_hrtimer *t;
	int ret = 0;

	BUILD_BUG_ON(MAX_CLOCKS != 16);
	BUILD_BUG_ON(sizeof(struct bpf_timer_kern) > sizeof(struct bpf_timer));
	BUILD_BUG_ON(__alignof__(struct bpf_timer_kern) != __alignof__(struct bpf_timer));

	if (in_nmi())
		return -EOPNOTSUPP;

	if (flags >= MAX_CLOCKS ||
	    /* similar to timerfd except _ALARM variants are not supported */
	    (clockid != CLOCK_MONOTONIC &&
	     clockid != CLOCK_REALTIME &&
	     clockid != CLOCK_BOOTTIME))
		return -EINVAL;
	__bpf_spin_lock_irqsave(&timer->lock);
	t = timer->timer;
	if (t) {
		ret = -EBUSY;
		goto out;
	}
	if (!atomic64_read(&map->usercnt)) {
		/* maps with timers must be either held by user space
		 * or pinned in bpffs.
		 */
		ret = -EPERM;
		goto out;
	}
	/* allocate hrtimer via map_kmalloc to use memcg accounting */
	t = bpf_map_kmalloc_node(map, sizeof(*t), GFP_ATOMIC, map->numa_node);
	if (!t) {
		ret = -ENOMEM;
		goto out;
	}
	t->value = (void *)timer - map->record->timer_off;
	t->map = map;
	t->prog = NULL;
	rcu_assign_pointer(t->callback_fn, NULL);
	hrtimer_init(&t->timer, clockid, HRTIMER_MODE_REL_SOFT);
	t->timer.function = bpf_timer_cb;
	timer->timer = t;
out:
	__bpf_spin_unlock_irqrestore(&timer->lock);
	return ret;
}

static const struct bpf_func_proto bpf_timer_init_proto = {
	.func		= bpf_timer_init,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_TIMER,
	.arg2_type	= ARG_CONST_MAP_PTR,
	.arg3_type	= ARG_ANYTHING,
};

BPF_CALL_3(bpf_timer_set_callback, struct bpf_timer_kern *, timer, void *, callback_fn,
	   struct bpf_prog_aux *, aux)
{
	struct bpf_prog *prev, *prog = aux->prog;
	struct bpf_hrtimer *t;
	int ret = 0;

	if (in_nmi())
		return -EOPNOTSUPP;
	__bpf_spin_lock_irqsave(&timer->lock);
	t = timer->timer;
	if (!t) {
		ret = -EINVAL;
		goto out;
	}
	if (!atomic64_read(&t->map->usercnt)) {
		/* maps with timers must be either held by user space
		 * or pinned in bpffs. Otherwise timer might still be
		 * running even when bpf prog is detached and user space
		 * is gone, since map_release_uref won't ever be called.
		 */
		ret = -EPERM;
		goto out;
	}
	prev = t->prog;
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
		t->prog = prog;
	}
	rcu_assign_pointer(t->callback_fn, callback_fn);
out:
	__bpf_spin_unlock_irqrestore(&timer->lock);
	return ret;
}

static const struct bpf_func_proto bpf_timer_set_callback_proto = {
	.func		= bpf_timer_set_callback,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_TIMER,
	.arg2_type	= ARG_PTR_TO_FUNC,
};

BPF_CALL_3(bpf_timer_start, struct bpf_timer_kern *, timer, u64, nsecs, u64, flags)
{
	struct bpf_hrtimer *t;
	int ret = 0;
	enum hrtimer_mode mode;

	if (in_nmi())
		return -EOPNOTSUPP;
	if (flags > BPF_F_TIMER_ABS)
		return -EINVAL;
	__bpf_spin_lock_irqsave(&timer->lock);
	t = timer->timer;
	if (!t || !t->prog) {
		ret = -EINVAL;
		goto out;
	}

	if (flags & BPF_F_TIMER_ABS)
		mode = HRTIMER_MODE_ABS_SOFT;
	else
		mode = HRTIMER_MODE_REL_SOFT;

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

static void drop_prog_refcnt(struct bpf_hrtimer *t)
{
	struct bpf_prog *prog = t->prog;

	if (prog) {
		bpf_prog_put(prog);
		t->prog = NULL;
		rcu_assign_pointer(t->callback_fn, NULL);
	}
}

BPF_CALL_1(bpf_timer_cancel, struct bpf_timer_kern *, timer)
{
	struct bpf_hrtimer *t;
	int ret = 0;

	if (in_nmi())
		return -EOPNOTSUPP;
	__bpf_spin_lock_irqsave(&timer->lock);
	t = timer->timer;
	if (!t) {
		ret = -EINVAL;
		goto out;
	}
	if (this_cpu_read(hrtimer_running) == t) {
		/* If bpf callback_fn is trying to bpf_timer_cancel()
		 * its own timer the hrtimer_cancel() will deadlock
		 * since it waits for callback_fn to finish
		 */
		ret = -EDEADLK;
		goto out;
	}
	drop_prog_refcnt(t);
out:
	__bpf_spin_unlock_irqrestore(&timer->lock);
	/* Cancel the timer and wait for associated callback to finish
	 * if it was running.
	 */
	ret = ret ?: hrtimer_cancel(&t->timer);
	return ret;
}

static const struct bpf_func_proto bpf_timer_cancel_proto = {
	.func		= bpf_timer_cancel,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_TIMER,
};

/* This function is called by map_delete/update_elem for individual element and
 * by ops->map_release_uref when the user space reference to a map reaches zero.
 */
void bpf_timer_cancel_and_free(void *val)
{
	struct bpf_timer_kern *timer = val;
	struct bpf_hrtimer *t;

	/* Performance optimization: read timer->timer without lock first. */
	if (!READ_ONCE(timer->timer))
		return;

	__bpf_spin_lock_irqsave(&timer->lock);
	/* re-read it under lock */
	t = timer->timer;
	if (!t)
		goto out;
	drop_prog_refcnt(t);
	/* The subsequent bpf_timer_start/cancel() helpers won't be able to use
	 * this timer, since it won't be initialized.
	 */
	timer->timer = NULL;
out:
	__bpf_spin_unlock_irqrestore(&timer->lock);
	if (!t)
		return;
	/* Cancel the timer and wait for callback to complete if it was running.
	 * If hrtimer_cancel() can be safely called it's safe to call kfree(t)
	 * right after for both preallocated and non-preallocated maps.
	 * The timer->timer = NULL was already done and no code path can
	 * see address 't' anymore.
	 *
	 * Check that bpf_map_delete/update_elem() wasn't called from timer
	 * callback_fn. In such case don't call hrtimer_cancel() (since it will
	 * deadlock) and don't call hrtimer_try_to_cancel() (since it will just
	 * return -1). Though callback_fn is still running on this cpu it's
	 * safe to do kfree(t) because bpf_timer_cb() read everything it needed
	 * from 't'. The bpf subprog callback_fn won't be able to access 't',
	 * since timer->timer = NULL was already done. The timer will be
	 * effectively cancelled because bpf_timer_cb() will return
	 * HRTIMER_NORESTART.
	 */
	if (this_cpu_read(hrtimer_running) != t)
		hrtimer_cancel(&t->timer);
	kfree(t);
}

BPF_CALL_2(bpf_kptr_xchg, void *, map_value, void *, ptr)
{
	unsigned long *kptr = map_value;

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
	.arg1_type    = ARG_PTR_TO_KPTR,
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

static bool __bpf_dynptr_is_rdonly(const struct bpf_dynptr_kern *ptr)
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

static int bpf_dynptr_check_off_len(const struct bpf_dynptr_kern *ptr, u32 offset, u32 len)
{
	u32 size = __bpf_dynptr_size(ptr);

	if (len > size || offset > size - len)
		return -E2BIG;

	return 0;
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
	.arg4_type	= ARG_PTR_TO_DYNPTR | DYNPTR_TYPE_LOCAL | MEM_UNINIT,
};

BPF_CALL_5(bpf_dynptr_read, void *, dst, u32, len, const struct bpf_dynptr_kern *, src,
	   u32, offset, u64, flags)
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

BPF_CALL_5(bpf_dynptr_write, const struct bpf_dynptr_kern *, dst, u32, offset, void *, src,
	   u32, len, u64, flags)
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

const struct bpf_func_proto *
bpf_base_func_proto(enum bpf_func_id func_id)
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
	default:
		break;
	}

	if (!bpf_capable())
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
#endif
	default:
		break;
	}

	if (!perfmon_capable())
		return NULL;

	switch (func_id) {
	case BPF_FUNC_trace_printk:
		return bpf_get_trace_printk_proto();
	case BPF_FUNC_get_current_task:
		return &bpf_get_current_task_proto;
	case BPF_FUNC_get_current_task_btf:
		return &bpf_get_current_task_btf_proto;
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
	case BPF_FUNC_snprintf_btf:
		return &bpf_snprintf_btf_proto;
	case BPF_FUNC_snprintf:
		return &bpf_snprintf_proto;
	case BPF_FUNC_task_pt_regs:
		return &bpf_task_pt_regs_proto;
	case BPF_FUNC_trace_vprintk:
		return bpf_get_trace_vprintk_proto();
	default:
		return NULL;
	}
}

void __bpf_obj_drop_impl(void *p, const struct btf_record *rec);

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
		migrate_disable();
		__bpf_obj_drop_impl(obj, field->graph_root.value_rec);
		migrate_enable();
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


		migrate_disable();
		__bpf_obj_drop_impl(obj, field->graph_root.value_rec);
		migrate_enable();
	}
}

__diag_push();
__diag_ignore_all("-Wmissing-prototypes",
		  "Global functions as their definitions will be in vmlinux BTF");

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

/* Must be called under migrate_disable(), as required by bpf_mem_free */
void __bpf_obj_drop_impl(void *p, const struct btf_record *rec)
{
	if (rec && rec->refcount_off >= 0 &&
	    !refcount_dec_and_test((refcount_t *)(p + rec->refcount_off))) {
		/* Object is refcounted and refcount_dec didn't result in 0
		 * refcount. Return without freeing the object
		 */
		return;
	}

	if (rec)
		bpf_obj_free_fields(rec, p);

	if (rec && rec->refcount_off >= 0)
		bpf_mem_free_rcu(&bpf_global_ma, p);
	else
		bpf_mem_free(&bpf_global_ma, p);
}

__bpf_kfunc void bpf_obj_drop_impl(void *p__alloc, void *meta__ign)
{
	struct btf_struct_meta *meta = meta__ign;
	void *p = p__alloc;

	__bpf_obj_drop_impl(p, meta ? meta->record : NULL);
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
		__bpf_obj_drop_impl((void *)n - off, rec);
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
		__bpf_obj_drop_impl((void *)n - off, rec);
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
	return task_under_cgroup_hierarchy(task, ancestor);
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
 * bpf_dynptr_slice() - Obtain a read-only pointer to the dynptr data.
 * @ptr: The dynptr whose data slice to retrieve
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
__bpf_kfunc void *bpf_dynptr_slice(const struct bpf_dynptr_kern *ptr, u32 offset,
				   void *buffer__opt, u32 buffer__szk)
{
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
 * @ptr: The dynptr whose data slice to retrieve
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
__bpf_kfunc void *bpf_dynptr_slice_rdwr(const struct bpf_dynptr_kern *ptr, u32 offset,
					void *buffer__opt, u32 buffer__szk)
{
	if (!ptr->data || __bpf_dynptr_is_rdonly(ptr))
		return NULL;

	/* bpf_dynptr_slice_rdwr is the same logic as bpf_dynptr_slice.
	 *
	 * For skb-type dynptrs, it is safe to write into the returned pointer
	 * if the bpf program allows skb data writes. There are two possiblities
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
	return bpf_dynptr_slice(ptr, offset, buffer__opt, buffer__szk);
}

__bpf_kfunc int bpf_dynptr_adjust(struct bpf_dynptr_kern *ptr, u32 start, u32 end)
{
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

__bpf_kfunc bool bpf_dynptr_is_null(struct bpf_dynptr_kern *ptr)
{
	return !ptr->data;
}

__bpf_kfunc bool bpf_dynptr_is_rdonly(struct bpf_dynptr_kern *ptr)
{
	if (!ptr->data)
		return false;

	return __bpf_dynptr_is_rdonly(ptr);
}

__bpf_kfunc __u32 bpf_dynptr_size(const struct bpf_dynptr_kern *ptr)
{
	if (!ptr->data)
		return -EINVAL;

	return __bpf_dynptr_size(ptr);
}

__bpf_kfunc int bpf_dynptr_clone(struct bpf_dynptr_kern *ptr,
				 struct bpf_dynptr_kern *clone__uninit)
{
	if (!ptr->data) {
		bpf_dynptr_set_null(clone__uninit);
		return -EINVAL;
	}

	*clone__uninit = *ptr;

	return 0;
}

__bpf_kfunc void *bpf_cast_to_kern_ctx(void *obj)
{
	return obj;
}

__bpf_kfunc void *bpf_rdonly_cast(void *obj__ign, u32 btf_id__k)
{
	return obj__ign;
}

__bpf_kfunc void bpf_rcu_read_lock(void)
{
	rcu_read_lock();
}

__bpf_kfunc void bpf_rcu_read_unlock(void)
{
	rcu_read_unlock();
}

__diag_pop();

BTF_SET8_START(generic_btf_ids)
#ifdef CONFIG_KEXEC_CORE
BTF_ID_FLAGS(func, crash_kexec, KF_DESTRUCTIVE)
#endif
BTF_ID_FLAGS(func, bpf_obj_new_impl, KF_ACQUIRE | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_obj_drop_impl, KF_RELEASE)
BTF_ID_FLAGS(func, bpf_refcount_acquire_impl, KF_ACQUIRE | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_list_push_front_impl)
BTF_ID_FLAGS(func, bpf_list_push_back_impl)
BTF_ID_FLAGS(func, bpf_list_pop_front, KF_ACQUIRE | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_list_pop_back, KF_ACQUIRE | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_task_acquire, KF_ACQUIRE | KF_RCU | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_task_release, KF_RELEASE)
BTF_ID_FLAGS(func, bpf_rbtree_remove, KF_ACQUIRE | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_rbtree_add_impl)
BTF_ID_FLAGS(func, bpf_rbtree_first, KF_RET_NULL)

#ifdef CONFIG_CGROUPS
BTF_ID_FLAGS(func, bpf_cgroup_acquire, KF_ACQUIRE | KF_RCU | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_cgroup_release, KF_RELEASE)
BTF_ID_FLAGS(func, bpf_cgroup_ancestor, KF_ACQUIRE | KF_RCU | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_cgroup_from_id, KF_ACQUIRE | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_task_under_cgroup, KF_RCU)
#endif
BTF_ID_FLAGS(func, bpf_task_from_pid, KF_ACQUIRE | KF_RET_NULL)
BTF_SET8_END(generic_btf_ids)

static const struct btf_kfunc_id_set generic_kfunc_set = {
	.owner = THIS_MODULE,
	.set   = &generic_btf_ids,
};


BTF_ID_LIST(generic_dtor_ids)
BTF_ID(struct, task_struct)
BTF_ID(func, bpf_task_release)
#ifdef CONFIG_CGROUPS
BTF_ID(struct, cgroup)
BTF_ID(func, bpf_cgroup_release)
#endif

BTF_SET8_START(common_btf_ids)
BTF_ID_FLAGS(func, bpf_cast_to_kern_ctx)
BTF_ID_FLAGS(func, bpf_rdonly_cast)
BTF_ID_FLAGS(func, bpf_rcu_read_lock)
BTF_ID_FLAGS(func, bpf_rcu_read_unlock)
BTF_ID_FLAGS(func, bpf_dynptr_slice, KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_dynptr_slice_rdwr, KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_iter_num_new, KF_ITER_NEW)
BTF_ID_FLAGS(func, bpf_iter_num_next, KF_ITER_NEXT | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_iter_num_destroy, KF_ITER_DESTROY)
BTF_ID_FLAGS(func, bpf_dynptr_adjust)
BTF_ID_FLAGS(func, bpf_dynptr_is_null)
BTF_ID_FLAGS(func, bpf_dynptr_is_rdonly)
BTF_ID_FLAGS(func, bpf_dynptr_size)
BTF_ID_FLAGS(func, bpf_dynptr_clone)
BTF_SET8_END(common_btf_ids)

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
	ret = ret ?: register_btf_kfunc_id_set(BPF_PROG_TYPE_STRUCT_OPS, &generic_kfunc_set);
	ret = ret ?: register_btf_id_dtor_kfuncs(generic_dtors,
						  ARRAY_SIZE(generic_dtors),
						  THIS_MODULE);
	return ret ?: register_btf_kfunc_id_set(BPF_PROG_TYPE_UNSPEC, &common_kfunc_set);
}

late_initcall(kfunc_init);
