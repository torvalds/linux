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
#include <linux/mmap_lock.h>
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
	struct stack_map_bucket *buckets[] __counted_by(n_buckets);
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

/**
 * stack_map_calculate_max_depth - Calculate maximum allowed stack trace depth
 * @size:  Size of the buffer/map value in bytes
 * @elem_size:  Size of each stack trace element
 * @flags:  BPF stack trace flags (BPF_F_USER_STACK, BPF_F_USER_BUILD_ID, ...)
 *
 * Return: Maximum number of stack trace entries that can be safely stored
 */
static u32 stack_map_calculate_max_depth(u32 size, u32 elem_size, u64 flags)
{
	u32 skip = flags & BPF_F_SKIP_FIELD_MASK;
	u32 max_depth;
	u32 curr_sysctl_max_stack = READ_ONCE(sysctl_perf_event_max_stack);

	max_depth = size / elem_size;
	max_depth += skip;
	if (max_depth > curr_sysctl_max_stack)
		return curr_sysctl_max_stack;

	return max_depth;
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

	/* hash table size must be power of 2; roundup_pow_of_two() can overflow
	 * into UB on 32-bit arches, so check that first
	 */
	if (attr->max_entries > 1UL << 31)
		return ERR_PTR(-E2BIG);

	n_buckets = roundup_pow_of_two(attr->max_entries);

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

static int fetch_build_id(struct vm_area_struct *vma, unsigned char *build_id, bool may_fault)
{
	return may_fault ? build_id_parse(vma, build_id, NULL)
			 : build_id_parse_nofault(vma, build_id, NULL);
}

static inline void stack_map_build_id_set_ip(struct bpf_stack_build_id *id)
{
	id->status = BPF_STACK_BUILD_ID_IP;
	memset(id->build_id, 0, BUILD_ID_SIZE_MAX);
}

static inline u64 stack_map_build_id_offset(unsigned long vm_pgoff,
					    unsigned long vm_start, u64 ip)
{
	return (vm_pgoff << PAGE_SHIFT) + ip - vm_start;
}

static inline void stack_map_build_id_set_valid(struct bpf_stack_build_id *id,
						u64 offset,
						const unsigned char *build_id)
{
	id->status = BPF_STACK_BUILD_ID_VALID;
	id->offset = offset;
	if (id->build_id != build_id)
		memcpy(id->build_id, build_id, BUILD_ID_SIZE_MAX);
}

/*
 * A cached VMA lookup result. The range [vm_start, vm_end) is always set.
 * vm_pgoff, file, build_id are set only when the build ID was resolved.
 * Zero vm_end marks the slot empty. build_id aliases the id_offs[] entry.
 */
struct stack_map_cached_vma {
	unsigned long vm_start;
	unsigned long vm_end;
	unsigned long vm_pgoff;
	struct file *file; /* pinned in the sleepable path; NULL otherwise */
	const unsigned char *build_id;
};

/*
 * Per stack_map_get_build_id_offset() call cache of the last VMA with a build ID
 * resolved and the last VMA with no usable build ID. Adjacent stack frames tend
 * to land in the same VMA or the same backing file, so caching the last result
 * of each kind lets us skip unnecessary VMA lookups and build ID parse calls.
 * Keeping the two slots independent means a build-ID-less VMA doesn't evict the
 * last resolved build ID.
 */
struct stack_map_build_id_cache {
	struct stack_map_cached_vma resolved;
	struct stack_map_cached_vma unresolved;
};

/*
 * Fill @id from a cached range covering @ip. On a hit this writes @id (resolved
 * range -> build ID + offset, unresolved range -> raw ip) and returns 0; on a
 * miss it leaves @id untouched and returns -ENOENT.
 */
static int stack_map_build_id_set_from_cache(struct stack_map_build_id_cache *cache,
					     struct bpf_stack_build_id *id, u64 ip)
{
	unsigned long vm_start, vm_end, vm_pgoff;
	u64 offset;

	vm_start = cache->resolved.vm_start;
	vm_end = cache->resolved.vm_end;
	if (vm_end && ip >= vm_start && ip < vm_end) {
		vm_pgoff = cache->resolved.vm_pgoff;
		offset = stack_map_build_id_offset(vm_pgoff, vm_start, ip);
		stack_map_build_id_set_valid(id, offset, cache->resolved.build_id);
		return 0;
	}

	vm_start = cache->unresolved.vm_start;
	vm_end = cache->unresolved.vm_end;
	if (vm_end && ip >= vm_start && ip < vm_end) {
		stack_map_build_id_set_ip(id);
		return 0;
	}

	return -ENOENT;
}

/*
 * Record @vma's build ID as the last resolved one. @file is the pinned backing
 * file in the sleepable path (released when evicted), or NULL otherwise.
 */
static void stack_map_build_id_cache_set_resolved(struct stack_map_build_id_cache *cache,
						  struct file *file,
						  const unsigned char *build_id,
						  unsigned long vm_start,
						  unsigned long vm_end,
						  unsigned long vm_pgoff)
{
	if (cache->resolved.file)
		fput(cache->resolved.file);
	cache->resolved = (struct stack_map_cached_vma){
		.vm_start = vm_start,
		.vm_end = vm_end,
		.vm_pgoff = vm_pgoff,
		.file = file,
		.build_id = build_id,
	};
}

/* Record [vm_start, vm_end) as a range with no usable build ID. */
static void stack_map_build_id_cache_set_unresolved(struct stack_map_build_id_cache *cache,
						    unsigned long vm_start,
						    unsigned long vm_end)
{
	cache->unresolved = (struct stack_map_cached_vma){
		.vm_start = vm_start,
		.vm_end = vm_end,
	};
}

struct stack_map_vma_lock {
	struct vm_area_struct *vma;
	struct mm_struct *mm;
};

/*
 * Acquire a stable read-side reference on the VMA covering @ip.
 *
 * With CONFIG_PER_VMA_LOCK=y this returns a VMA with its per-VMA read
 * lock held and mmap_lock dropped, so the caller may sleep.
 *
 * With CONFIG_PER_VMA_LOCK=n it returns a VMA with mmap_lock still
 * held; the caller must snapshot any fields it needs and pin vm_file
 * with get_file() before stack_map_unlock_vma() drops mmap_lock, as
 * the VMA may be split, merged, or freed after that.
 *
 * Returns NULL on failure, in which case no lock is held.
 */
static struct vm_area_struct *
stack_map_lock_vma(struct stack_map_vma_lock *lock, unsigned long ip)
{
	struct mm_struct *mm = lock->mm;
	struct vm_area_struct *vma;

	/* noop under !CONFIG_PER_VMA_LOCK */
	vma = lock_vma_under_rcu(mm, ip);
	if (vma) {
		lock->vma = vma;
		return vma;
	}

	/*
	 * Taking mmap_read_lock() is unsafe here, because the caller BPF
	 * program might already hold it, causing a deadlock.
	 */
	if (!mmap_read_trylock(mm))
		return NULL;

	vma = vma_lookup(mm, ip);
	if (!vma) {
		mmap_read_unlock(mm);
		return NULL;
	}

#ifdef CONFIG_PER_VMA_LOCK
	if (!vma_start_read_locked(vma)) {
		mmap_read_unlock(mm);
		return NULL;
	}
	mmap_read_unlock(mm);
#endif

	lock->vma = vma;
	return vma;
}

static void stack_map_unlock_vma(struct stack_map_vma_lock *lock)
{
#ifdef CONFIG_PER_VMA_LOCK
	vma_end_read(lock->vma);
#else
	mmap_read_unlock(lock->mm);
#endif
	lock->vma = NULL;
}

static void stack_map_get_build_id_offset_sleepable(struct bpf_stack_build_id *id_offs,
						    u32 trace_nr)
{
	struct stack_map_vma_lock lock = { .mm = current->mm };
	struct stack_map_build_id_cache cache = {};
	struct stack_map_cached_vma *res = &cache.resolved;
	unsigned long vm_pgoff, vm_start, vm_end;
	struct vm_area_struct *vma;
	struct file *file;
	u64 offset;
	u64 ip;

	for (u32 i = 0; i < trace_nr; i++) {
		ip = READ_ONCE(id_offs[i].ip);

		if (!stack_map_build_id_set_from_cache(&cache, &id_offs[i], ip))
			continue;

		vma = stack_map_lock_vma(&lock, ip);
		if (!vma) {
			stack_map_build_id_set_ip(&id_offs[i]);
			continue;
		}

		vm_pgoff = vma->vm_pgoff;
		vm_start = vma->vm_start;
		vm_end = vma->vm_end;

		if (vma_is_anonymous(vma) || !vma->vm_file) {
			stack_map_unlock_vma(&lock);
			stack_map_build_id_set_ip(&id_offs[i]);
			stack_map_build_id_cache_set_unresolved(&cache, vm_start, vm_end);
			continue;
		}

		file = vma->vm_file;
		offset = stack_map_build_id_offset(vm_pgoff, vm_start, ip);

		/*
		 * Same backing file as the last resolved VMA (another mapping
		 * of the same ELF binary): reuse its build_id without re-parsing.
		 */
		if (file == res->file) {
			stack_map_unlock_vma(&lock);
			stack_map_build_id_set_valid(&id_offs[i], offset, res->build_id);
			res->vm_start = vm_start;
			res->vm_end = vm_end;
			res->vm_pgoff = vm_pgoff;
			continue;
		}

		file = get_file(file);
		stack_map_unlock_vma(&lock);

		/* build_id_parse_file() may block on filesystem reads */
		if (build_id_parse_file(file, id_offs[i].build_id, NULL)) {
			stack_map_build_id_set_ip(&id_offs[i]);
			fput(file);
			stack_map_build_id_cache_set_unresolved(&cache, vm_start, vm_end);
			continue;
		}

		stack_map_build_id_set_valid(&id_offs[i], offset, id_offs[i].build_id);
		stack_map_build_id_cache_set_resolved(&cache, file, id_offs[i].build_id,
						      vm_start, vm_end, vm_pgoff);
	}

	if (res->file)
		fput(res->file);
}

/*
 * Expects all id_offs[i].ip values to be set to correct initial IPs.
 * They will be subsequently:
 *   - either adjusted in place to a file offset, if build ID fetching
 *     succeeds; in this case id_offs[i].build_id is set to correct build ID,
 *     and id_offs[i].status is set to BPF_STACK_BUILD_ID_VALID;
 *   - or IP will be kept intact, if build ID fetching failed; in this case
 *     id_offs[i].build_id is zeroed out and id_offs[i].status is set to
 *     BPF_STACK_BUILD_ID_IP.
 */
static void stack_map_get_build_id_offset(struct bpf_stack_build_id *id_offs,
					  u32 trace_nr, bool user, bool may_fault)
{
	struct mmap_unlock_irq_work *work = NULL;
	bool irq_work_busy = bpf_mmap_unlock_get_irq_work(&work);
	bool has_user_ctx = user && current && current->mm;
	struct stack_map_build_id_cache cache = {};
	struct vm_area_struct *vma;
	int i;

	if (may_fault && has_user_ctx) {
		stack_map_get_build_id_offset_sleepable(id_offs, trace_nr);
		return;
	}

	/* If the irq_work is in use, fall back to report ips. Same
	 * fallback is used for kernel stack (!user) on a stackmap with
	 * build_id.
	 */
	if (!has_user_ctx || irq_work_busy || !mmap_read_trylock(current->mm)) {
		/* cannot access current->mm, fall back to ips */
		for (i = 0; i < trace_nr; i++)
			stack_map_build_id_set_ip(&id_offs[i]);
		return;
	}

	for (i = 0; i < trace_nr; i++) {
		u64 ip = READ_ONCE(id_offs[i].ip);

		if (!stack_map_build_id_set_from_cache(&cache, &id_offs[i], ip))
			continue;

		vma = find_vma(current->mm, ip);
		if (!vma || vma_is_anonymous(vma) ||
		    fetch_build_id(vma, id_offs[i].build_id, may_fault)) {
			/* per entry fall back to ips; cache build-ID-less range */
			stack_map_build_id_set_ip(&id_offs[i]);
			if (vma)
				stack_map_build_id_cache_set_unresolved(&cache,
						vma->vm_start, vma->vm_end);
			continue;
		}
		/*
		 * mmap_lock is held for the whole loop, so the cached VMA
		 * fields stay valid; no file pinning is needed here.
		 */
		stack_map_build_id_set_valid(&id_offs[i],
			stack_map_build_id_offset(vma->vm_pgoff, vma->vm_start, ip),
			id_offs[i].build_id);
		stack_map_build_id_cache_set_resolved(&cache, NULL, id_offs[i].build_id,
						      vma->vm_start, vma->vm_end,
						      vma->vm_pgoff);
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
	u32 hash, id, trace_nr, trace_len, i, max_depth;
	u32 skip = flags & BPF_F_SKIP_FIELD_MASK;
	bool user = flags & BPF_F_USER_STACK;
	u64 *ips;
	bool hash_matches;

	if (trace->nr <= skip)
		/* skipping more than usable stack trace */
		return -EFAULT;

	max_depth = stack_map_calculate_max_depth(map->value_size, stack_map_data_size(map), flags);
	trace_nr = min_t(u32, trace->nr - skip, max_depth - skip);
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
		struct bpf_stack_build_id *id_offs;

		/* for build_id+offset, pop a bucket before slow cmp */
		new_bucket = (struct stack_map_bucket *)
			pcpu_freelist_pop(&smap->freelist);
		if (unlikely(!new_bucket))
			return -ENOMEM;
		new_bucket->nr = trace_nr;
		id_offs = (struct bpf_stack_build_id *)new_bucket->data;
		for (i = 0; i < trace_nr; i++)
			id_offs[i].ip = ips[i];
		stack_map_get_build_id_offset(id_offs, trace_nr, user, false /* !may_fault */);
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
	u32 elem_size = stack_map_data_size(map);
	bool user = flags & BPF_F_USER_STACK;
	struct perf_callchain_entry *trace;
	bool kernel = !user;
	u32 max_depth;

	if (unlikely(flags & ~(BPF_F_SKIP_FIELD_MASK | BPF_F_USER_STACK |
			       BPF_F_FAST_STACK_CMP | BPF_F_REUSE_STACKID)))
		return -EINVAL;

	max_depth = stack_map_calculate_max_depth(map->value_size, elem_size, flags);
	trace = get_perf_callchain(regs, kernel, user, max_depth,
				   false, false, 0);

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
	if (!(event->attr.sample_type & PERF_SAMPLE_CALLCHAIN))
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
	__u64 nr = trace->nr; /* save original */

	if (kernel) {
		trace->nr = nr_kernel;
		ret = __bpf_get_stackid(map, trace, flags);
	} else { /* user */
		u64 skip = flags & BPF_F_SKIP_FIELD_MASK;

		skip += nr_kernel;
		if (skip > BPF_F_SKIP_FIELD_MASK)
			return -EFAULT;

		flags = (flags & ~BPF_F_SKIP_FIELD_MASK) | skip;
		ret = __bpf_get_stackid(map, trace, flags);
	}

	/* restore nr */
	trace->nr = nr;

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
			    void *buf, u32 size, u64 flags, bool may_fault)
{
	u32 trace_nr, copy_len, elem_size, max_depth;
	bool user_build_id = flags & BPF_F_USER_BUILD_ID;
	bool crosstask = task && task != current;
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

	elem_size = user_build_id ? sizeof(struct bpf_stack_build_id) : sizeof(u64);
	if (unlikely(size % elem_size))
		goto clear;

	/* cannot get valid user stack for task without user_mode regs */
	if (task && user && !user_mode(regs))
		goto err_fault;

	/* get_perf_callchain does not support crosstask user stack walking
	 * but returns an empty stack instead of NULL.
	 */
	if (crosstask && user) {
		err = -EOPNOTSUPP;
		goto clear;
	}

	max_depth = stack_map_calculate_max_depth(size, elem_size, flags);

	if (may_fault)
		rcu_read_lock(); /* need RCU for perf's callchain below */

	if (trace_in) {
		trace = trace_in;
		trace->nr = min_t(u32, trace->nr, max_depth);
	} else if (kernel && task) {
		trace = get_callchain_entry_for_task(task, max_depth);
	} else {
		trace = get_perf_callchain(regs, kernel, user, max_depth,
					   crosstask, false, 0);
	}

	if (unlikely(!trace) || trace->nr < skip) {
		if (may_fault)
			rcu_read_unlock();
		goto err_fault;
	}

	trace_nr = trace->nr - skip;
	copy_len = trace_nr * elem_size;

	ips = trace->ip + skip;
	if (user_build_id) {
		struct bpf_stack_build_id *id_offs = buf;
		u32 i;

		for (i = 0; i < trace_nr; i++)
			id_offs[i].ip = ips[i];
	} else {
		memcpy(buf, ips, copy_len);
	}

	/* trace/ips should not be dereferenced after this point */
	if (may_fault)
		rcu_read_unlock();

	if (user_build_id)
		stack_map_get_build_id_offset(buf, trace_nr, user, may_fault);

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
	return __bpf_get_stack(regs, NULL, NULL, buf, size, flags, false /* !may_fault */);
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

BPF_CALL_4(bpf_get_stack_sleepable, struct pt_regs *, regs, void *, buf, u32, size,
	   u64, flags)
{
	return __bpf_get_stack(regs, NULL, NULL, buf, size, flags, true /* may_fault */);
}

const struct bpf_func_proto bpf_get_stack_sleepable_proto = {
	.func		= bpf_get_stack_sleepable,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg3_type	= ARG_CONST_SIZE_OR_ZERO,
	.arg4_type	= ARG_ANYTHING,
};

static long __bpf_get_task_stack(struct task_struct *task, void *buf, u32 size,
				 u64 flags, bool may_fault)
{
	struct pt_regs *regs;
	long res = -EINVAL;

	if (!try_get_task_stack(task))
		return -EFAULT;

	regs = task_pt_regs(task);
	if (regs)
		res = __bpf_get_stack(regs, task, NULL, buf, size, flags, may_fault);
	put_task_stack(task);

	return res;
}

BPF_CALL_4(bpf_get_task_stack, struct task_struct *, task, void *, buf,
	   u32, size, u64, flags)
{
	return __bpf_get_task_stack(task, buf, size, flags, false /* !may_fault */);
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

BPF_CALL_4(bpf_get_task_stack_sleepable, struct task_struct *, task, void *, buf,
	   u32, size, u64, flags)
{
	return __bpf_get_task_stack(task, buf, size, flags, true /* !may_fault */);
}

const struct bpf_func_proto bpf_get_task_stack_sleepable_proto = {
	.func		= bpf_get_task_stack_sleepable,
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

	if (!(event->attr.sample_type & PERF_SAMPLE_CALLCHAIN))
		return __bpf_get_stack(regs, NULL, NULL, buf, size, flags, false /* !may_fault */);

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
		err = __bpf_get_stack(regs, NULL, trace, buf, size, flags, false /* !may_fault */);

		/* restore nr */
		trace->nr = nr;
	} else { /* user */
		u64 skip = flags & BPF_F_SKIP_FIELD_MASK;

		skip += nr_kernel;
		if (skip > BPF_F_SKIP_FIELD_MASK)
			goto clear;

		flags = (flags & ~BPF_F_SKIP_FIELD_MASK) | skip;
		err = __bpf_get_stack(regs, NULL, trace, buf, size, flags, false /* !may_fault */);
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
static int stack_map_lookup_and_delete_elem(struct bpf_map *map, void *key,
					    void *value, u64 flags)
{
	return bpf_stackmap_extract(map, key, value, true);
}

/* Called from syscall */
int bpf_stackmap_extract(struct bpf_map *map, void *key, void *value,
			 bool delete)
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

	if (delete)
		old_bucket = bucket;
	else
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

static long stack_map_update_elem(struct bpf_map *map, void *key, void *value,
				  u64 map_flags)
{
	return -EINVAL;
}

/* Called from syscall or from eBPF program */
static long stack_map_delete_elem(struct bpf_map *map, void *key)
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

static u64 stack_map_mem_usage(const struct bpf_map *map)
{
	struct bpf_stack_map *smap = container_of(map, struct bpf_stack_map, map);
	u64 value_size = map->value_size;
	u64 n_buckets = smap->n_buckets;
	u64 enties = map->max_entries;
	u64 usage = sizeof(*smap);

	usage += n_buckets * sizeof(struct stack_map_bucket *);
	usage += enties * (sizeof(struct stack_map_bucket) + value_size);
	return usage;
}

BTF_ID_LIST_SINGLE(stack_trace_map_btf_ids, struct, bpf_stack_map)
const struct bpf_map_ops stack_trace_map_ops = {
	.map_meta_equal = bpf_map_meta_equal,
	.map_alloc = stack_map_alloc,
	.map_free = stack_map_free,
	.map_get_next_key = stack_map_get_next_key,
	.map_lookup_elem = stack_map_lookup_elem,
	.map_lookup_and_delete_elem = stack_map_lookup_and_delete_elem,
	.map_update_elem = stack_map_update_elem,
	.map_delete_elem = stack_map_delete_elem,
	.map_check_btf = map_check_no_btf,
	.map_mem_usage = stack_map_mem_usage,
	.map_btf_id = &stack_trace_map_btf_ids[0],
};
