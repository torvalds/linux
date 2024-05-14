// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2011-2014 PLUMgrid, http://plumgrid.com
 */
#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/bpf-cgroup.h>
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
#include <linux/security.h>
#include <linux/btf_ids.h>

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
	strscpy(buf, task->comm, size);
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
	arch_spin_lock(l);
}

static inline void __bpf_spin_unlock(struct bpf_spin_lock *lock)
{
	arch_spinlock_t *l = (void *)lock;

	arch_spin_unlock(l);
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
};

void copy_map_value_locked(struct bpf_map *map, void *dst, void *src,
			   bool lock_src)
{
	struct bpf_spin_lock *lock;

	if (lock_src)
		lock = src + map->spin_lock_off;
	else
		lock = dst + map->spin_lock_off;
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
	.arg1_type	= ARG_PTR_TO_MEM,
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
#define MAX_BPRINTF_BUF_LEN	512

/* Support executing three nested bprintf helper calls on a given CPU */
#define MAX_BPRINTF_NEST_LEVEL	3
struct bpf_bprintf_buffers {
	char tmp_bufs[MAX_BPRINTF_NEST_LEVEL][MAX_BPRINTF_BUF_LEN];
};
static DEFINE_PER_CPU(struct bpf_bprintf_buffers, bpf_bprintf_bufs);
static DEFINE_PER_CPU(int, bpf_bprintf_nest_level);

static int try_get_fmt_tmp_buf(char **tmp_buf)
{
	struct bpf_bprintf_buffers *bufs;
	int nest_level;

	preempt_disable();
	nest_level = this_cpu_inc_return(bpf_bprintf_nest_level);
	if (WARN_ON_ONCE(nest_level > MAX_BPRINTF_NEST_LEVEL)) {
		this_cpu_dec(bpf_bprintf_nest_level);
		preempt_enable();
		return -EBUSY;
	}
	bufs = this_cpu_ptr(&bpf_bprintf_bufs);
	*tmp_buf = bufs->tmp_bufs[nest_level - 1];

	return 0;
}

void bpf_bprintf_cleanup(void)
{
	if (this_cpu_read(bpf_bprintf_nest_level)) {
		this_cpu_dec(bpf_bprintf_nest_level);
		preempt_enable();
	}
}

/*
 * bpf_bprintf_prepare - Generic pass on format strings for bprintf-like helpers
 *
 * Returns a negative value if fmt is an invalid format string or 0 otherwise.
 *
 * This can be used in two ways:
 * - Format string verification only: when bin_args is NULL
 * - Arguments preparation: in addition to the above verification, it writes in
 *   bin_args a binary representation of arguments usable by bstr_printf where
 *   pointers from BPF have been sanitized.
 *
 * In argument preparation mode, if 0 is returned, safe temporary buffers are
 * allocated and bpf_bprintf_cleanup should be called to free them after use.
 */
int bpf_bprintf_prepare(char *fmt, u32 fmt_size, const u64 *raw_args,
			u32 **bin_args, u32 num_args)
{
	char *unsafe_ptr = NULL, *tmp_buf = NULL, *tmp_buf_end, *fmt_end;
	size_t sizeof_cur_arg, sizeof_cur_ip;
	int err, i, num_spec = 0;
	u64 cur_arg;
	char fmt_ptype, cur_ip[16], ip_spec[] = "%pXX";

	fmt_end = strnchr(fmt, fmt_size, 0);
	if (!fmt_end)
		return -EINVAL;
	fmt_size = fmt_end - fmt;

	if (bin_args) {
		if (num_args && try_get_fmt_tmp_buf(&tmp_buf))
			return -EBUSY;

		tmp_buf_end = tmp_buf + MAX_BPRINTF_BUF_LEN;
		*bin_args = (u32 *)tmp_buf;
	}

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
		bpf_bprintf_cleanup();
	return err;
}

BPF_CALL_5(bpf_snprintf, char *, str, u32, str_size, char *, fmt,
	   const void *, data, u32, data_len)
{
	int err, num_args;
	u32 *bin_args;

	if (data_len % 8 || data_len > MAX_BPRINTF_VARARGS * 8 ||
	    (data_len && !data))
		return -EINVAL;
	num_args = data_len / 8;

	/* ARG_PTR_TO_CONST_STR guarantees that fmt is zero-terminated so we
	 * can safely give an unbounded size.
	 */
	err = bpf_bprintf_prepare(fmt, UINT_MAX, data, &bin_args, num_args);
	if (err < 0)
		return err;

	err = bstr_printf(str, str_size, fmt, bin_args);

	bpf_bprintf_cleanup();

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
	/* allocate hrtimer via map_kmalloc to use memcg accounting */
	t = bpf_map_kmalloc_node(map, sizeof(*t), GFP_ATOMIC, map->numa_node);
	if (!t) {
		ret = -ENOMEM;
		goto out;
	}
	t->value = (void *)timer - map->timer_off;
	t->map = map;
	t->prog = NULL;
	rcu_assign_pointer(t->callback_fn, NULL);
	hrtimer_init(&t->timer, clockid, HRTIMER_MODE_REL_SOFT);
	t->timer.function = bpf_timer_cb;
	WRITE_ONCE(timer->timer, t);
	/* Guarantee the order between timer->timer and map->usercnt. So
	 * when there are concurrent uref release and bpf timer init, either
	 * bpf_timer_cancel_and_free() called by uref release reads a no-NULL
	 * timer or atomic64_read() below returns a zero usercnt.
	 */
	smp_mb();
	if (!atomic64_read(&map->usercnt)) {
		/* maps with timers must be either held by user space
		 * or pinned in bpffs.
		 */
		WRITE_ONCE(timer->timer, NULL);
		kfree(t);
		ret = -EPERM;
	}
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

	if (in_nmi())
		return -EOPNOTSUPP;
	if (flags)
		return -EINVAL;
	__bpf_spin_lock_irqsave(&timer->lock);
	t = timer->timer;
	if (!t || !t->prog) {
		ret = -EINVAL;
		goto out;
	}
	hrtimer_start(&t->timer, ns_to_ktime(nsecs), HRTIMER_MODE_REL_SOFT);
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
	WRITE_ONCE(timer->timer, NULL);
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

static bool bpf_dynptr_is_rdonly(struct bpf_dynptr_kern *ptr)
{
	return ptr->size & DYNPTR_RDONLY_BIT;
}

static void bpf_dynptr_set_type(struct bpf_dynptr_kern *ptr, enum bpf_dynptr_type type)
{
	ptr->size |= type << DYNPTR_TYPE_SHIFT;
}

u32 bpf_dynptr_get_size(struct bpf_dynptr_kern *ptr)
{
	return ptr->size & DYNPTR_SIZE_MASK;
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

static int bpf_dynptr_check_off_len(struct bpf_dynptr_kern *ptr, u32 offset, u32 len)
{
	u32 size = bpf_dynptr_get_size(ptr);

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

BPF_CALL_5(bpf_dynptr_read, void *, dst, u32, len, struct bpf_dynptr_kern *, src,
	   u32, offset, u64, flags)
{
	int err;

	if (!src->data || flags)
		return -EINVAL;

	err = bpf_dynptr_check_off_len(src, offset, len);
	if (err)
		return err;

	memcpy(dst, src->data + src->offset + offset, len);

	return 0;
}

static const struct bpf_func_proto bpf_dynptr_read_proto = {
	.func		= bpf_dynptr_read,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg2_type	= ARG_CONST_SIZE_OR_ZERO,
	.arg3_type	= ARG_PTR_TO_DYNPTR,
	.arg4_type	= ARG_ANYTHING,
	.arg5_type	= ARG_ANYTHING,
};

BPF_CALL_5(bpf_dynptr_write, struct bpf_dynptr_kern *, dst, u32, offset, void *, src,
	   u32, len, u64, flags)
{
	int err;

	if (!dst->data || flags || bpf_dynptr_is_rdonly(dst))
		return -EINVAL;

	err = bpf_dynptr_check_off_len(dst, offset, len);
	if (err)
		return err;

	memcpy(dst->data + dst->offset + offset, src, len);

	return 0;
}

static const struct bpf_func_proto bpf_dynptr_write_proto = {
	.func		= bpf_dynptr_write,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_DYNPTR,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_PTR_TO_MEM | MEM_RDONLY,
	.arg4_type	= ARG_CONST_SIZE_OR_ZERO,
	.arg5_type	= ARG_ANYTHING,
};

BPF_CALL_3(bpf_dynptr_data, struct bpf_dynptr_kern *, ptr, u32, offset, u32, len)
{
	int err;

	if (!ptr->data)
		return 0;

	err = bpf_dynptr_check_off_len(ptr, offset, len);
	if (err)
		return 0;

	if (bpf_dynptr_is_rdonly(ptr))
		return 0;

	return (unsigned long)(ptr->data + ptr->offset + offset);
}

static const struct bpf_func_proto bpf_dynptr_data_proto = {
	.func		= bpf_dynptr_data,
	.gpl_only	= false,
	.ret_type	= RET_PTR_TO_DYNPTR_MEM_OR_NULL,
	.arg1_type	= ARG_PTR_TO_DYNPTR,
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

BTF_SET8_START(tracing_btf_ids)
#ifdef CONFIG_KEXEC_CORE
BTF_ID_FLAGS(func, crash_kexec, KF_DESTRUCTIVE)
#endif
BTF_SET8_END(tracing_btf_ids)

static const struct btf_kfunc_id_set tracing_kfunc_set = {
	.owner = THIS_MODULE,
	.set   = &tracing_btf_ids,
};

static int __init kfunc_init(void)
{
	return register_btf_kfunc_id_set(BPF_PROG_TYPE_TRACING, &tracing_kfunc_set);
}

late_initcall(kfunc_init);
