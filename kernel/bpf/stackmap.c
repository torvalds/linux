// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2016 Facebook
 */
#include <linux/bpf.h>
#include <linux/jhash.h>
#include <linux/filter.h>
#include <linux/kernel.h>
#include <linux/stacktrace.h>
#include <linux/perf_event.h>
#include <linux/btf_ids.h>
#include <linux/buildid.h>
#include "percpu_freelist.h"
#include "mmap_unlock_work.h"

#define STACK_CREATE_FLAG_MASK					\
	(BPF_F_NUMA_NODE | BPF_F_RDONLY | BPF_F_WRONLY |	\
	 BPF_F_STACK_BUILD_ID)

struct stack_map_bucket {
	struct pcpu_freelist_node fnode;
	u32 hash;
	u32 nr;
	u64 data[];
};

struct bpf_stack_map {
	struct bpf_map map;
	void *elems;
	struct pcpu_freelist freelist;
	u32 n_buckets;
	struct stack_map_bucket *buckets[];
};

static inline bool stack_map_use_build_id(struct bpf_map *map)
{
	return (map->map_flags & BPF_F_STACK_BUILD_ID);
}

static inline int stack_map_data_size(struct bpf_map *map)
{
	return stack_map_use_build_id(map) ?
		sizeof(struct bpf_stack_build_id) : sizeof(u64);
}

static int prealloc_elems_and_freelist(struct bpf_stack_map *smap)
{
	u64 elem_size = sizeof(struct stack_map_bucket) +
			(u64)smap->map.value_size;
	int err;

	smap->elems = bpf_map_area_alloc(elem_size * smap->map.max_entries,
					 smap->map.numa_node);
	if (!smap->elems)
		return -ENOMEM;

	err = pcpu_freelist_init(&smap->freelist);
	if (err)
		goto free_elems;

	pcpu_freelist_populate(&smap->freelist, smap->elems, elem_size,
			       smap->map.max_entries);
	return 0;

free_elems:
	bpf_map_area_free(smap->elems);
	return err;
}

/* Called from syscall */
static struct bpf_map *stack_map_alloc(union bpf_attr *attr)
{
	u32 value_size = attr->value_size;
	struct bpf_stack_map *smap;
	u64 cost, n_buckets;
	int err;

	if (!bpf_capable())
		return ERR_PTR(-EPERM);

	if (attr->map_flags & ~STACK_CREATE_FLAG_MASK)
		return ERR_PTR(-EINVAL);

	/* check sanity of attributes */
	if (attr->max_entries == 0 || attr->key_size != 4 ||
	    value_size < 8 || value_size % 8)
		return ERR_PTR(-EINVAL);

	BUILD_BUG_ON(sizeof(struct bpf_stack_build_id) % sizeof(u64));
	if (attr->map_flags & BPF_F_STACK_BUILD_ID) {
		if (value_size % sizeof(struct bpf_stack_build_id) ||
		    value_size / sizeof(struct bpf_stack_build_id)
		    > sysctl_perf_event_max_stack)
			return ERR_PTR(-EINVAL);
	} else if (value_size / 8 > sysctl_perf_event_max_stack)
		return ERR_PTR(-EINVAL);

	/* hash table size must be power of 2 */
	n_buckets = roundup_pow_of_two(attr->max_entries);
	if (!n_buckets)
		return ERR_PTR(-E2BIG);

	cost = n_buckets * sizeof(struct stack_map_bucket *) + sizeof(*smap);
	smap = bpf_map_area_alloc(cost, bpf_map_attr_numa_node(attr));
	if (!smap)
		return ERR_PTR(-ENOMEM);

	bpf_map_init_from_attr(&smap->map, attr);
	smap->n_buckets = n_buckets;

	err = get_callchain_buffers(sysctl_perf_event_max_stack);
	if (err)
		goto free_smap;

	err = prealloc_elems_and_freelist(smap);
	if (err)
		goto put_buffers;

	return &smap->map;

put_buffers:
	put_callchain_buffers();
free_smap:
	bpf_map_area_free(smap);
	return ERR_PTR(err);
}

static void stack_map_get_build_id_offset(struct bpf_stack_build_id *id_offs,
					  u64 *ips, u32 trace_nr, bool user)
{
	int i;
	struct mmap_unlock_irq_work *work = NULL;
	bool irq_work_busy = bpf_mmap_unlock_get_irq_work(&work);
	struct vm_area_struct *vma, *prev_vma = NULL;
	const char *prev_build_id;

	/* If the irq_work is in use, fall back to report ips. Same
	 * fallback is used for kernel stack (!user) on a stackmap with
	 * build_id.
	 */
	if (!user || !current || !current->mm || irq_work_busy ||
	    !mmap_read_trylock(current->mm)) {
		/* cannot access current->mm, fall back to ips */
		for (i = 0; i < trace_nr; i++) {
			id_offs[i].status = BPF_STACK_BUILD_ID_IP;
			id_offs[i].ip = ips[i];
			memset(id_offs[i].build_id, 0, BUILD_ID_SIZE_MAX);
		}
		return;
	}

	for (i = 0; i < trace_nr; i++) {
		if (range_in_vma(prev_vma, ips[i], ips[i])) {
			vma = prev_vma;
			memcpy(id_offs[i].build_id, prev_build_id,
			       BUILD_ID_SIZE_MAX);
			goto build_id_valid;
		}
		vma = find_vma(current->mm, ips[i]);
		if (!vma || build_id_parse(vma, id_offs[i].build_id, NULL)) {
			/* per entry fall back to ips */
			id_offs[i].status = BPF_STACK_BUILD_ID_IP;
			id_offs[i].ip = ips[i];
			memset(id_offs[i].build_id, 0, BUILD_ID_SIZE_MAX);
			continue;
		}
build_id_valid:
		id_offs[i].offset = (vma->vm_pgoff << PAGE_SHIFT) + ips[i]
			- vma->vm_start;
		id_offs[i].status = BPF_STACK_BUILD_ID_VALID;
		prev_vma = vma;
		prev_build_id = id_offs[i].build_id;
	}
	bpf_mmap_unlock_mm(work, current->mm);
}

static struct perf_callchain_entry *
get_callchain_entry_for_task(struct task_struct *task, u32 max_depth)
{
#ifdef CONFIG_STACKTRACE
	struct perf_callchain_entry *entry;
	int rctx;

	entry = get_callchain_entry(&rctx);

	if (!entry)
		return NULL;

	entry->nr = stack_trace_save_tsk(task, (unsigned long *)entry->ip,
					 max_depth, 0);

	/* stack_trace_save_tsk() works on unsigned long array, while
	 * perf_callchain_entry uses u64 array. For 32-bit systems, it is
	 * necessary to fix this mismatch.
	 */
	if (__BITS_PER_LONG != 64) {
		unsigned long *from = (unsigned long *) entry->ip;
		u64 *to = entry->ip;
		int i;

		/* copy data from the end to avoid using extra buffer */
		for (i = entry->nr - 1; i >= 0; i--)
			to[i] = (u64)(from[i]);
	}

	put_callchain_entry(rctx);

	return entry;
#else /* CONFIG_STACKTRACE */
	return NULL;
#endif
}

static long __bpf_get_stackid(struct bpf_map *map,
			      struct perf_callchain_entry *trace, u64 flags)
{
	struct bpf_stack_map *smap = container_of(map, struct bpf_stack_map, map);
	struct stack_map_bucket *bucket, *new_bucket, *old_bucket;
	u32 skip = flags & BPF_F_SKIP_FIELD_MASK;
	u32 hash, id, trace_nr, trace_len;
	bool user = flags & BPF_F_USER_STACK;
	u64 *ips;
	bool hash_matches;

	if (trace->nr <= skip)
		/* skipping more than usable stack trace */
		return -EFAULT;

	trace_nr = trace->nr - skip;
	trace_len = trace_nr * sizeof(u64);
	ips = trace->ip + skip;
	hash = jhash2((u32 *)ips, trace_len / sizeof(u32), 0);
	id = hash & (smap->n_buckets - 1);
	bucket = READ_ONCE(smap->buckets[id]);

	hash_matches = bucket && bucket->hash == hash;
	/* fast cmp */
	if (hash_matches && flags & BPF_F_FAST_STACK_CMP)
		return id;

	if (stack_map_use_build_id(map)) {
		/* for build_id+offset, pop a bucket before slow cmp */
		new_bucket = (struct stack_map_bucket *)
			pcpu_freelist_pop(&smap->freelist);
		if (unlikely(!new_bucket))
			return -ENOMEM;
		new_bucket->nr = trace_nr;
		stack_map_get_build_id_offset(
			(struct bpf_stack_build_id *)new_bucket->data,
			ips, trace_nr, user);
		trace_len = trace_nr * sizeof(struct bpf_stack_build_id);
		if (hash_matches && bucket->nr == trace_nr &&
		    memcmp(bucket->data, new_bucket->data, trace_len) == 0) {
			pcpu_freelist_push(&smap->freelist, &new_bucket->fnode);
			return id;
		}
		if (bucket && !(flags & BPF_F_REUSE_STACKID)) {
			pcpu_freelist_push(&smap->freelist, &new_bucket->fnode);
			return -EEXIST;
		}
	} else {
		if (hash_matches && bucket->nr == trace_nr &&
		    memcmp(bucket->data, ips, trace_len) == 0)
			return id;
		if (bucket && !(flags & BPF_F_REUSE_STACKID))
			return -EEXIST;

		new_bucket = (struct stack_map_bucket *)
			pcpu_freelist_pop(&smap->freelist);
		if (unlikely(!new_bucket))
			return -ENOMEM;
		memcpy(new_bucket->data, ips, trace_len);
	}

	new_bucket->hash = hash;
	new_bucket->nr = trace_nr;

	old_bucket = xchg(&smap->buckets[id], new_bucket);
	if (old_bucket)
		pcpu_freelist_push(&smap->freelist, &old_bucket->fnode);
	return id;
}

BPF_CALL_3(bpf_get_stackid, struct pt_regs *, regs, struct bpf_map *, map,
	   u64, flags)
{
	u32 max_depth = map->value_size / stack_map_data_size(map);
	u32 skip = flags & BPF_F_SKIP_FIELD_MASK;
	bool user = flags & BPF_F_USER_STACK;
	struct perf_callchain_entry *trace;
	bool kernel = !user;

	if (unlikely(flags & ~(BPF_F_SKIP_FIELD_MASK | BPF_F_USER_STACK |
			       BPF_F_FAST_STACK_CMP | BPF_F_REUSE_STACKID)))
		return -EINVAL;

	max_depth += skip;
	if (max_depth > sysctl_perf_event_max_stack)
		max_depth = sysctl_perf_event_max_stack;

	trace = get_perf_callchain(regs, 0, kernel, user, max_depth,
				   false, false);

	if (unlikely(!trace))
		/* couldn't fetch the stack trace */
		return -EFAULT;

	return __bpf_get_stackid(map, trace, flags);
}

const struct bpf_func_proto bpf_get_stackid_proto = {
	.func		= bpf_get_stackid,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_CONST_MAP_PTR,
	.arg3_type	= ARG_ANYTHING,
};

static __u64 count_kernel_ip(struct perf_callchain_entry *trace)
{
	__u64 nr_kernel = 0;

	while (nr_kernel < trace->nr) {
		if (trace->ip[nr_kernel] == PERF_CONTEXT_USER)
			break;
		nr_kernel++;
	}
	return nr_kernel;
}

BPF_CALL_3(bpf_get_stackid_pe, struct bpf_perf_event_data_kern *, ctx,
	   struct bpf_map *, map, u64, flags)
{
	struct perf_event *event = ctx->event;
	struct perf_callchain_entry *trace;
	bool kernel, user;
	__u64 nr_kernel;
	int ret;

	/* perf_sample_data doesn't have callchain, use bpf_get_stackid */
	if (!(event->attr.sample_type & __PERF_SAMPLE_CALLCHAIN_EARLY))
		return bpf_get_stackid((unsigned long)(ctx->regs),
				       (unsigned long) map, flags, 0, 0);

	if (unlikely(flags & ~(BPF_F_SKIP_FIELD_MASK | BPF_F_USER_STACK |
			       BPF_F_FAST_STACK_CMP | BPF_F_REUSE_STACKID)))
		return -EINVAL;

	user = flags & BPF_F_USER_STACK;
	kernel = !user;

	trace = ctx->data->callchain;
	if (unlikely(!trace))
		return -EFAULT;

	nr_kernel = count_kernel_ip(trace);

	if (kernel) {
		__u64 nr = trace->nr;

		trace->nr = nr_kernel;
		ret = __bpf_get_stackid(map, trace, flags);

		/* restore nr */
		trace->nr = nr;
	} else { /* user */
		u64 skip = flags & BPF_F_SKIP_FIELD_MASK;

		skip += nr_kernel;
		if (skip > BPF_F_SKIP_FIELD_MASK)
			return -EFAULT;

		flags = (flags & ~BPF_F_SKIP_FIELD_MASK) | skip;
		ret = __bpf_get_stackid(map, trace, flags);
	}
	return ret;
}

const struct bpf_func_proto bpf_get_stackid_proto_pe = {
	.func		= bpf_get_stackid_pe,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_CONST_MAP_PTR,
	.arg3_type	= ARG_ANYTHING,
};

static long __bpf_get_stack(struct pt_regs *regs, struct task_struct *task,
			    struct perf_callchain_entry *trace_in,
			    void *buf, u32 size, u64 flags)
{
	u32 trace_nr, copy_len, elem_size, num_elem, max_depth;
	bool user_build_id = flags & BPF_F_USER_BUILD_ID;
	u32 skip = flags & BPF_F_SKIP_FIELD_MASK;
	bool user = flags & BPF_F_USER_STACK;
	struct perf_callchain_entry *trace;
	bool kernel = !user;
	int err = -EINVAL;
	u64 *ips;

	if (unlikely(flags & ~(BPF_F_SKIP_FIELD_MASK | BPF_F_USER_STACK |
			       BPF_F_USER_BUILD_ID)))
		goto clear;
	if (kernel && user_build_id)
		goto clear;

	elem_size = (user && user_build_id) ? sizeof(struct bpf_stack_build_id)
					    : sizeof(u64);
	if (unlikely(size % elem_size))
		goto clear;

	/* cannot get valid user stack for task without user_mode regs */
	if (task && user && !user_mode(regs))
		goto err_fault;

	num_elem = size / elem_size;
	max_depth = num_elem + skip;
	if (sysctl_perf_event_max_stack < max_depth)
		max_depth = sysctl_perf_event_max_stack;

	if (trace_in)
		trace = trace_in;
	else if (kernel && task)
		trace = get_callchain_entry_for_task(task, max_depth);
	else
		trace = get_perf_callchain(regs, 0, kernel, user, max_depth,
					   false, false);
	if (unlikely(!trace))
		goto err_fault;

	if (trace->nr < skip)
		goto err_fault;

	trace_nr = trace->nr - skip;
	trace_nr = (trace_nr <= num_elem) ? trace_nr : num_elem;
	copy_len = trace_nr * elem_size;

	ips = trace->ip + skip;
	if (user && user_build_id)
		stack_map_get_build_id_offset(buf, ips, trace_nr, user);
	else
		memcpy(buf, ips, copy_len);

	if (size > copy_len)
		memset(buf + copy_len, 0, size - copy_len);
	return copy_len;

err_fault:
	err = -EFAULT;
clear:
	memset(buf, 0, size);
	return err;
}

BPF_CALL_4(bpf_get_stack, struct pt_regs *, regs, void *, buf, u32, size,
	   u64, flags)
{
	return __bpf_get_stack(regs, NULL, NULL, buf, size, flags);
}

const struct bpf_func_proto bpf_get_stack_proto = {
	.func		= bpf_get_stack,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg3_type	= ARG_CONST_SIZE_OR_ZERO,
	.arg4_type	= ARG_ANYTHING,
};

BPF_CALL_4(bpf_get_task_stack, struct task_struct *, task, void *, buf,
	   u32, size, u64, flags)
{
	struct pt_regs *regs;
	long res = -EINVAL;

	if (!try_get_task_stack(task))
		return -EFAULT;

	regs = task_pt_regs(task);
	if (regs)
		res = __bpf_get_stack(regs, task, NULL, buf, size, flags);
	put_task_stack(task);

	return res;
}

const struct bpf_func_proto bpf_get_task_stack_proto = {
	.func		= bpf_get_task_stack,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_BTF_ID,
	.arg1_btf_id	= &btf_tracing_ids[BTF_TRACING_TYPE_TASK],
	.arg2_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg3_type	= ARG_CONST_SIZE_OR_ZERO,
	.arg4_type	= ARG_ANYTHING,
};

BPF_CALL_4(bpf_get_stack_pe, struct bpf_perf_event_data_kern *, ctx,
	   void *, buf, u32, size, u64, flags)
{
	struct pt_regs *regs = (struct pt_regs *)(ctx->regs);
	struct perf_event *event = ctx->event;
	struct perf_callchain_entry *trace;
	bool kernel, user;
	int err = -EINVAL;
	__u64 nr_kernel;

	if (!(event->attr.sample_type & __PERF_SAMPLE_CALLCHAIN_EARLY))
		return __bpf_get_stack(regs, NULL, NULL, buf, size, flags);

	if (unlikely(flags & ~(BPF_F_SKIP_FIELD_MASK | BPF_F_USER_STACK |
			       BPF_F_USER_BUILD_ID)))
		goto clear;

	user = flags & BPF_F_USER_STACK;
	kernel = !user;

	err = -EFAULT;
	trace = ctx->data->callchain;
	if (unlikely(!trace))
		goto clear;

	nr_kernel = count_kernel_ip(trace);

	if (kernel) {
		__u64 nr = trace->nr;

		trace->nr = nr_kernel;
		err = __bpf_get_stack(regs, NULL, trace, buf, size, flags);

		/* restore nr */
		trace->nr = nr;
	} else { /* user */
		u64 skip = flags & BPF_F_SKIP_FIELD_MASK;

		skip += nr_kernel;
		if (skip > BPF_F_SKIP_FIELD_MASK)
			goto clear;

		flags = (flags & ~BPF_F_SKIP_FIELD_MASK) | skip;
		err = __bpf_get_stack(regs, NULL, trace, buf, size, flags);
	}
	return err;

clear:
	memset(buf, 0, size);
	return err;

}

const struct bpf_func_proto bpf_get_stack_proto_pe = {
	.func		= bpf_get_stack_pe,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg3_type	= ARG_CONST_SIZE_OR_ZERO,
	.arg4_type	= ARG_ANYTHING,
};

/* Called from eBPF program */
static void *stack_map_lookup_elem(struct bpf_map *map, void *key)
{
	return ERR_PTR(-EOPNOTSUPP);
}

/* Called from syscall */
int bpf_stackmap_copy(struct bpf_map *map, void *key, void *value)
{
	struct bpf_stack_map *smap = container_of(map, struct bpf_stack_map, map);
	struct stack_map_bucket *bucket, *old_bucket;
	u32 id = *(u32 *)key, trace_len;

	if (unlikely(id >= smap->n_buckets))
		return -ENOENT;

	bucket = xchg(&smap->buckets[id], NULL);
	if (!bucket)
		return -ENOENT;

	trace_len = bucket->nr * stack_map_data_size(map);
	memcpy(value, bucket->data, trace_len);
	memset(value + trace_len, 0, map->value_size - trace_len);

	old_bucket = xchg(&smap->buckets[id], bucket);
	if (old_bucket)
		pcpu_freelist_push(&smap->freelist, &old_bucket->fnode);
	return 0;
}

static int stack_map_get_next_key(struct bpf_map *map, void *key,
				  void *next_key)
{
	struct bpf_stack_map *smap = container_of(map,
						  struct bpf_stack_map, map);
	u32 id;

	WARN_ON_ONCE(!rcu_read_lock_held());

	if (!key) {
		id = 0;
	} else {
		id = *(u32 *)key;
		if (id >= smap->n_buckets || !smap->buckets[id])
			id = 0;
		else
			id++;
	}

	while (id < smap->n_buckets && !smap->buckets[id])
		id++;

	if (id >= smap->n_buckets)
		return -ENOENT;

	*(u32 *)next_key = id;
	return 0;
}

static int stack_map_update_elem(struct bpf_map *map, void *key, void *value,
				 u64 map_flags)
{
	return -EINVAL;
}

/* Called from syscall or from eBPF program */
static int stack_map_delete_elem(struct bpf_map *map, void *key)
{
	struct bpf_stack_map *smap = container_of(map, struct bpf_stack_map, map);
	struct stack_map_bucket *old_bucket;
	u32 id = *(u32 *)key;

	if (unlikely(id >= smap->n_buckets))
		return -E2BIG;

	old_bucket = xchg(&smap->buckets[id], NULL);
	if (old_bucket) {
		pcpu_freelist_push(&smap->freelist, &old_bucket->fnode);
		return 0;
	} else {
		return -ENOENT;
	}
}

/* Called when map->refcnt goes to zero, either from workqueue or from syscall */
static void stack_map_free(struct bpf_map *map)
{
	struct bpf_stack_map *smap = container_of(map, struct bpf_stack_map, map);

	bpf_map_area_free(smap->elems);
	pcpu_freelist_destroy(&smap->freelist);
	bpf_map_area_free(smap);
	put_callchain_buffers();
}

BTF_ID_LIST_SINGLE(stack_trace_map_btf_ids, struct, bpf_stack_map)
const struct bpf_map_ops stack_trace_map_ops = {
	.map_meta_equal = bpf_map_meta_equal,
	.map_alloc = stack_map_alloc,
	.map_free = stack_map_free,
	.map_get_next_key = stack_map_get_next_key,
	.map_lookup_elem = stack_map_lookup_elem,
	.map_update_elem = stack_map_update_elem,
	.map_delete_elem = stack_map_delete_elem,
	.map_check_btf = map_check_no_btf,
	.map_btf_id = &stack_trace_map_btf_ids[0],
};
