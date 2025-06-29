// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2011-2014 PLUMgrid, http://plumgrid.com
 */
#include <linux/bpf.h>
#include <linux/bpf-cgroup.h>
#include <linux/bpf_trace.h>
#include <linux/bpf_lirc.h>
#include <linux/bpf_verifier.h>
#include <linux/bsearch.h>
#include <linux/btf.h>
#include <linux/syscalls.h>
#include <linux/slab.h>
#include <linux/sched/signal.h>
#include <linux/vmalloc.h>
#include <linux/mmzone.h>
#include <linux/anon_inodes.h>
#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/license.h>
#include <linux/filter.h>
#include <linux/kernel.h>
#include <linux/idr.h>
#include <linux/cred.h>
#include <linux/timekeeping.h>
#include <linux/ctype.h>
#include <linux/nospec.h>
#include <linux/audit.h>
#include <uapi/linux/btf.h>
#include <linux/pgtable.h>
#include <linux/bpf_lsm.h>
#include <linux/poll.h>
#include <linux/sort.h>
#include <linux/bpf-netns.h>
#include <linux/rcupdate_trace.h>
#include <linux/memcontrol.h>
#include <linux/trace_events.h>
#include <linux/tracepoint.h>
#include <linux/overflow.h>

#include <net/netfilter/nf_bpf_link.h>
#include <net/netkit.h>
#include <net/tcx.h>

#define IS_FD_ARRAY(map) ((map)->map_type == BPF_MAP_TYPE_PERF_EVENT_ARRAY || \
			  (map)->map_type == BPF_MAP_TYPE_CGROUP_ARRAY || \
			  (map)->map_type == BPF_MAP_TYPE_ARRAY_OF_MAPS)
#define IS_FD_PROG_ARRAY(map) ((map)->map_type == BPF_MAP_TYPE_PROG_ARRAY)
#define IS_FD_HASH(map) ((map)->map_type == BPF_MAP_TYPE_HASH_OF_MAPS)
#define IS_FD_MAP(map) (IS_FD_ARRAY(map) || IS_FD_PROG_ARRAY(map) || \
			IS_FD_HASH(map))

#define BPF_OBJ_FLAG_MASK   (BPF_F_RDONLY | BPF_F_WRONLY)

DEFINE_PER_CPU(int, bpf_prog_active);
static DEFINE_IDR(prog_idr);
static DEFINE_SPINLOCK(prog_idr_lock);
static DEFINE_IDR(map_idr);
static DEFINE_SPINLOCK(map_idr_lock);
static DEFINE_IDR(link_idr);
static DEFINE_SPINLOCK(link_idr_lock);

int sysctl_unprivileged_bpf_disabled __read_mostly =
	IS_BUILTIN(CONFIG_BPF_UNPRIV_DEFAULT_OFF) ? 2 : 0;

static const struct bpf_map_ops * const bpf_map_types[] = {
#define BPF_PROG_TYPE(_id, _name, prog_ctx_type, kern_ctx_type)
#define BPF_MAP_TYPE(_id, _ops) \
	[_id] = &_ops,
#define BPF_LINK_TYPE(_id, _name)
#include <linux/bpf_types.h>
#undef BPF_PROG_TYPE
#undef BPF_MAP_TYPE
#undef BPF_LINK_TYPE
};

/*
 * If we're handed a bigger struct than we know of, ensure all the unknown bits
 * are 0 - i.e. new user-space does not rely on any kernel feature extensions
 * we don't know about yet.
 *
 * There is a ToCToU between this function call and the following
 * copy_from_user() call. However, this is not a concern since this function is
 * meant to be a future-proofing of bits.
 */
int bpf_check_uarg_tail_zero(bpfptr_t uaddr,
			     size_t expected_size,
			     size_t actual_size)
{
	int res;

	if (unlikely(actual_size > PAGE_SIZE))	/* silly large */
		return -E2BIG;

	if (actual_size <= expected_size)
		return 0;

	if (uaddr.is_kernel)
		res = memchr_inv(uaddr.kernel + expected_size, 0,
				 actual_size - expected_size) == NULL;
	else
		res = check_zeroed_user(uaddr.user + expected_size,
					actual_size - expected_size);
	if (res < 0)
		return res;
	return res ? 0 : -E2BIG;
}

const struct bpf_map_ops bpf_map_offload_ops = {
	.map_meta_equal = bpf_map_meta_equal,
	.map_alloc = bpf_map_offload_map_alloc,
	.map_free = bpf_map_offload_map_free,
	.map_check_btf = map_check_no_btf,
	.map_mem_usage = bpf_map_offload_map_mem_usage,
};

static void bpf_map_write_active_inc(struct bpf_map *map)
{
	atomic64_inc(&map->writecnt);
}

static void bpf_map_write_active_dec(struct bpf_map *map)
{
	atomic64_dec(&map->writecnt);
}

bool bpf_map_write_active(const struct bpf_map *map)
{
	return atomic64_read(&map->writecnt) != 0;
}

static u32 bpf_map_value_size(const struct bpf_map *map)
{
	if (map->map_type == BPF_MAP_TYPE_PERCPU_HASH ||
	    map->map_type == BPF_MAP_TYPE_LRU_PERCPU_HASH ||
	    map->map_type == BPF_MAP_TYPE_PERCPU_ARRAY ||
	    map->map_type == BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE)
		return round_up(map->value_size, 8) * num_possible_cpus();
	else if (IS_FD_MAP(map))
		return sizeof(u32);
	else
		return  map->value_size;
}

static void maybe_wait_bpf_programs(struct bpf_map *map)
{
	/* Wait for any running non-sleepable BPF programs to complete so that
	 * userspace, when we return to it, knows that all non-sleepable
	 * programs that could be running use the new map value. For sleepable
	 * BPF programs, synchronize_rcu_tasks_trace() should be used to wait
	 * for the completions of these programs, but considering the waiting
	 * time can be very long and userspace may think it will hang forever,
	 * so don't handle sleepable BPF programs now.
	 */
	if (map->map_type == BPF_MAP_TYPE_HASH_OF_MAPS ||
	    map->map_type == BPF_MAP_TYPE_ARRAY_OF_MAPS)
		synchronize_rcu();
}

static void unpin_uptr_kaddr(void *kaddr)
{
	if (kaddr)
		unpin_user_page(virt_to_page(kaddr));
}

static void __bpf_obj_unpin_uptrs(struct btf_record *rec, u32 cnt, void *obj)
{
	const struct btf_field *field;
	void **uptr_addr;
	int i;

	for (i = 0, field = rec->fields; i < cnt; i++, field++) {
		if (field->type != BPF_UPTR)
			continue;

		uptr_addr = obj + field->offset;
		unpin_uptr_kaddr(*uptr_addr);
	}
}

static void bpf_obj_unpin_uptrs(struct btf_record *rec, void *obj)
{
	if (!btf_record_has_field(rec, BPF_UPTR))
		return;

	__bpf_obj_unpin_uptrs(rec, rec->cnt, obj);
}

static int bpf_obj_pin_uptrs(struct btf_record *rec, void *obj)
{
	const struct btf_field *field;
	const struct btf_type *t;
	unsigned long start, end;
	struct page *page;
	void **uptr_addr;
	int i, err;

	if (!btf_record_has_field(rec, BPF_UPTR))
		return 0;

	for (i = 0, field = rec->fields; i < rec->cnt; i++, field++) {
		if (field->type != BPF_UPTR)
			continue;

		uptr_addr = obj + field->offset;
		start = *(unsigned long *)uptr_addr;
		if (!start)
			continue;

		t = btf_type_by_id(field->kptr.btf, field->kptr.btf_id);
		/* t->size was checked for zero before */
		if (check_add_overflow(start, t->size - 1, &end)) {
			err = -EFAULT;
			goto unpin_all;
		}

		/* The uptr's struct cannot span across two pages */
		if ((start & PAGE_MASK) != (end & PAGE_MASK)) {
			err = -EOPNOTSUPP;
			goto unpin_all;
		}

		err = pin_user_pages_fast(start, 1, FOLL_LONGTERM | FOLL_WRITE, &page);
		if (err != 1)
			goto unpin_all;

		if (PageHighMem(page)) {
			err = -EOPNOTSUPP;
			unpin_user_page(page);
			goto unpin_all;
		}

		*uptr_addr = page_address(page) + offset_in_page(start);
	}

	return 0;

unpin_all:
	__bpf_obj_unpin_uptrs(rec, i, obj);
	return err;
}

static int bpf_map_update_value(struct bpf_map *map, struct file *map_file,
				void *key, void *value, __u64 flags)
{
	int err;

	/* Need to create a kthread, thus must support schedule */
	if (bpf_map_is_offloaded(map)) {
		return bpf_map_offload_update_elem(map, key, value, flags);
	} else if (map->map_type == BPF_MAP_TYPE_CPUMAP ||
		   map->map_type == BPF_MAP_TYPE_ARENA ||
		   map->map_type == BPF_MAP_TYPE_STRUCT_OPS) {
		return map->ops->map_update_elem(map, key, value, flags);
	} else if (map->map_type == BPF_MAP_TYPE_SOCKHASH ||
		   map->map_type == BPF_MAP_TYPE_SOCKMAP) {
		return sock_map_update_elem_sys(map, key, value, flags);
	} else if (IS_FD_PROG_ARRAY(map)) {
		return bpf_fd_array_map_update_elem(map, map_file, key, value,
						    flags);
	}

	bpf_disable_instrumentation();
	if (map->map_type == BPF_MAP_TYPE_PERCPU_HASH ||
	    map->map_type == BPF_MAP_TYPE_LRU_PERCPU_HASH) {
		err = bpf_percpu_hash_update(map, key, value, flags);
	} else if (map->map_type == BPF_MAP_TYPE_PERCPU_ARRAY) {
		err = bpf_percpu_array_update(map, key, value, flags);
	} else if (map->map_type == BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE) {
		err = bpf_percpu_cgroup_storage_update(map, key, value,
						       flags);
	} else if (IS_FD_ARRAY(map)) {
		err = bpf_fd_array_map_update_elem(map, map_file, key, value,
						   flags);
	} else if (map->map_type == BPF_MAP_TYPE_HASH_OF_MAPS) {
		err = bpf_fd_htab_map_update_elem(map, map_file, key, value,
						  flags);
	} else if (map->map_type == BPF_MAP_TYPE_REUSEPORT_SOCKARRAY) {
		/* rcu_read_lock() is not needed */
		err = bpf_fd_reuseport_array_update_elem(map, key, value,
							 flags);
	} else if (map->map_type == BPF_MAP_TYPE_QUEUE ||
		   map->map_type == BPF_MAP_TYPE_STACK ||
		   map->map_type == BPF_MAP_TYPE_BLOOM_FILTER) {
		err = map->ops->map_push_elem(map, value, flags);
	} else {
		err = bpf_obj_pin_uptrs(map->record, value);
		if (!err) {
			rcu_read_lock();
			err = map->ops->map_update_elem(map, key, value, flags);
			rcu_read_unlock();
			if (err)
				bpf_obj_unpin_uptrs(map->record, value);
		}
	}
	bpf_enable_instrumentation();

	return err;
}

static int bpf_map_copy_value(struct bpf_map *map, void *key, void *value,
			      __u64 flags)
{
	void *ptr;
	int err;

	if (bpf_map_is_offloaded(map))
		return bpf_map_offload_lookup_elem(map, key, value);

	bpf_disable_instrumentation();
	if (map->map_type == BPF_MAP_TYPE_PERCPU_HASH ||
	    map->map_type == BPF_MAP_TYPE_LRU_PERCPU_HASH) {
		err = bpf_percpu_hash_copy(map, key, value);
	} else if (map->map_type == BPF_MAP_TYPE_PERCPU_ARRAY) {
		err = bpf_percpu_array_copy(map, key, value);
	} else if (map->map_type == BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE) {
		err = bpf_percpu_cgroup_storage_copy(map, key, value);
	} else if (map->map_type == BPF_MAP_TYPE_STACK_TRACE) {
		err = bpf_stackmap_copy(map, key, value);
	} else if (IS_FD_ARRAY(map) || IS_FD_PROG_ARRAY(map)) {
		err = bpf_fd_array_map_lookup_elem(map, key, value);
	} else if (IS_FD_HASH(map)) {
		err = bpf_fd_htab_map_lookup_elem(map, key, value);
	} else if (map->map_type == BPF_MAP_TYPE_REUSEPORT_SOCKARRAY) {
		err = bpf_fd_reuseport_array_lookup_elem(map, key, value);
	} else if (map->map_type == BPF_MAP_TYPE_QUEUE ||
		   map->map_type == BPF_MAP_TYPE_STACK ||
		   map->map_type == BPF_MAP_TYPE_BLOOM_FILTER) {
		err = map->ops->map_peek_elem(map, value);
	} else if (map->map_type == BPF_MAP_TYPE_STRUCT_OPS) {
		/* struct_ops map requires directly updating "value" */
		err = bpf_struct_ops_map_sys_lookup_elem(map, key, value);
	} else {
		rcu_read_lock();
		if (map->ops->map_lookup_elem_sys_only)
			ptr = map->ops->map_lookup_elem_sys_only(map, key);
		else
			ptr = map->ops->map_lookup_elem(map, key);
		if (IS_ERR(ptr)) {
			err = PTR_ERR(ptr);
		} else if (!ptr) {
			err = -ENOENT;
		} else {
			err = 0;
			if (flags & BPF_F_LOCK)
				/* lock 'ptr' and copy everything but lock */
				copy_map_value_locked(map, value, ptr, true);
			else
				copy_map_value(map, value, ptr);
			/* mask lock and timer, since value wasn't zero inited */
			check_and_init_map_value(map, value);
		}
		rcu_read_unlock();
	}

	bpf_enable_instrumentation();

	return err;
}

/* Please, do not use this function outside from the map creation path
 * (e.g. in map update path) without taking care of setting the active
 * memory cgroup (see at bpf_map_kmalloc_node() for example).
 */
static void *__bpf_map_area_alloc(u64 size, int numa_node, bool mmapable)
{
	/* We really just want to fail instead of triggering OOM killer
	 * under memory pressure, therefore we set __GFP_NORETRY to kmalloc,
	 * which is used for lower order allocation requests.
	 *
	 * It has been observed that higher order allocation requests done by
	 * vmalloc with __GFP_NORETRY being set might fail due to not trying
	 * to reclaim memory from the page cache, thus we set
	 * __GFP_RETRY_MAYFAIL to avoid such situations.
	 */

	gfp_t gfp = bpf_memcg_flags(__GFP_NOWARN | __GFP_ZERO);
	unsigned int flags = 0;
	unsigned long align = 1;
	void *area;

	if (size >= SIZE_MAX)
		return NULL;

	/* kmalloc()'ed memory can't be mmap()'ed */
	if (mmapable) {
		BUG_ON(!PAGE_ALIGNED(size));
		align = SHMLBA;
		flags = VM_USERMAP;
	} else if (size <= (PAGE_SIZE << PAGE_ALLOC_COSTLY_ORDER)) {
		area = kmalloc_node(size, gfp | GFP_USER | __GFP_NORETRY,
				    numa_node);
		if (area != NULL)
			return area;
	}

	return __vmalloc_node_range(size, align, VMALLOC_START, VMALLOC_END,
			gfp | GFP_KERNEL | __GFP_RETRY_MAYFAIL, PAGE_KERNEL,
			flags, numa_node, __builtin_return_address(0));
}

void *bpf_map_area_alloc(u64 size, int numa_node)
{
	return __bpf_map_area_alloc(size, numa_node, false);
}

void *bpf_map_area_mmapable_alloc(u64 size, int numa_node)
{
	return __bpf_map_area_alloc(size, numa_node, true);
}

void bpf_map_area_free(void *area)
{
	kvfree(area);
}

static u32 bpf_map_flags_retain_permanent(u32 flags)
{
	/* Some map creation flags are not tied to the map object but
	 * rather to the map fd instead, so they have no meaning upon
	 * map object inspection since multiple file descriptors with
	 * different (access) properties can exist here. Thus, given
	 * this has zero meaning for the map itself, lets clear these
	 * from here.
	 */
	return flags & ~(BPF_F_RDONLY | BPF_F_WRONLY);
}

void bpf_map_init_from_attr(struct bpf_map *map, union bpf_attr *attr)
{
	map->map_type = attr->map_type;
	map->key_size = attr->key_size;
	map->value_size = attr->value_size;
	map->max_entries = attr->max_entries;
	map->map_flags = bpf_map_flags_retain_permanent(attr->map_flags);
	map->numa_node = bpf_map_attr_numa_node(attr);
	map->map_extra = attr->map_extra;
}

static int bpf_map_alloc_id(struct bpf_map *map)
{
	int id;

	idr_preload(GFP_KERNEL);
	spin_lock_bh(&map_idr_lock);
	id = idr_alloc_cyclic(&map_idr, map, 1, INT_MAX, GFP_ATOMIC);
	if (id > 0)
		map->id = id;
	spin_unlock_bh(&map_idr_lock);
	idr_preload_end();

	if (WARN_ON_ONCE(!id))
		return -ENOSPC;

	return id > 0 ? 0 : id;
}

void bpf_map_free_id(struct bpf_map *map)
{
	unsigned long flags;

	/* Offloaded maps are removed from the IDR store when their device
	 * disappears - even if someone holds an fd to them they are unusable,
	 * the memory is gone, all ops will fail; they are simply waiting for
	 * refcnt to drop to be freed.
	 */
	if (!map->id)
		return;

	spin_lock_irqsave(&map_idr_lock, flags);

	idr_remove(&map_idr, map->id);
	map->id = 0;

	spin_unlock_irqrestore(&map_idr_lock, flags);
}

#ifdef CONFIG_MEMCG
static void bpf_map_save_memcg(struct bpf_map *map)
{
	/* Currently if a map is created by a process belonging to the root
	 * memory cgroup, get_obj_cgroup_from_current() will return NULL.
	 * So we have to check map->objcg for being NULL each time it's
	 * being used.
	 */
	if (memcg_bpf_enabled())
		map->objcg = get_obj_cgroup_from_current();
}

static void bpf_map_release_memcg(struct bpf_map *map)
{
	if (map->objcg)
		obj_cgroup_put(map->objcg);
}

static struct mem_cgroup *bpf_map_get_memcg(const struct bpf_map *map)
{
	if (map->objcg)
		return get_mem_cgroup_from_objcg(map->objcg);

	return root_mem_cgroup;
}

void *bpf_map_kmalloc_node(const struct bpf_map *map, size_t size, gfp_t flags,
			   int node)
{
	struct mem_cgroup *memcg, *old_memcg;
	void *ptr;

	memcg = bpf_map_get_memcg(map);
	old_memcg = set_active_memcg(memcg);
	ptr = kmalloc_node(size, flags | __GFP_ACCOUNT, node);
	set_active_memcg(old_memcg);
	mem_cgroup_put(memcg);

	return ptr;
}

void *bpf_map_kzalloc(const struct bpf_map *map, size_t size, gfp_t flags)
{
	struct mem_cgroup *memcg, *old_memcg;
	void *ptr;

	memcg = bpf_map_get_memcg(map);
	old_memcg = set_active_memcg(memcg);
	ptr = kzalloc(size, flags | __GFP_ACCOUNT);
	set_active_memcg(old_memcg);
	mem_cgroup_put(memcg);

	return ptr;
}

void *bpf_map_kvcalloc(struct bpf_map *map, size_t n, size_t size,
		       gfp_t flags)
{
	struct mem_cgroup *memcg, *old_memcg;
	void *ptr;

	memcg = bpf_map_get_memcg(map);
	old_memcg = set_active_memcg(memcg);
	ptr = kvcalloc(n, size, flags | __GFP_ACCOUNT);
	set_active_memcg(old_memcg);
	mem_cgroup_put(memcg);

	return ptr;
}

void __percpu *bpf_map_alloc_percpu(const struct bpf_map *map, size_t size,
				    size_t align, gfp_t flags)
{
	struct mem_cgroup *memcg, *old_memcg;
	void __percpu *ptr;

	memcg = bpf_map_get_memcg(map);
	old_memcg = set_active_memcg(memcg);
	ptr = __alloc_percpu_gfp(size, align, flags | __GFP_ACCOUNT);
	set_active_memcg(old_memcg);
	mem_cgroup_put(memcg);

	return ptr;
}

#else
static void bpf_map_save_memcg(struct bpf_map *map)
{
}

static void bpf_map_release_memcg(struct bpf_map *map)
{
}
#endif

static bool can_alloc_pages(void)
{
	return preempt_count() == 0 && !irqs_disabled() &&
		!IS_ENABLED(CONFIG_PREEMPT_RT);
}

static struct page *__bpf_alloc_page(int nid)
{
	if (!can_alloc_pages())
		return alloc_pages_nolock(nid, 0);

	return alloc_pages_node(nid,
				GFP_KERNEL | __GFP_ZERO | __GFP_ACCOUNT
				| __GFP_NOWARN,
				0);
}

int bpf_map_alloc_pages(const struct bpf_map *map, int nid,
			unsigned long nr_pages, struct page **pages)
{
	unsigned long i, j;
	struct page *pg;
	int ret = 0;
#ifdef CONFIG_MEMCG
	struct mem_cgroup *memcg, *old_memcg;

	memcg = bpf_map_get_memcg(map);
	old_memcg = set_active_memcg(memcg);
#endif
	for (i = 0; i < nr_pages; i++) {
		pg = __bpf_alloc_page(nid);

		if (pg) {
			pages[i] = pg;
			continue;
		}
		for (j = 0; j < i; j++)
			free_pages_nolock(pages[j], 0);
		ret = -ENOMEM;
		break;
	}

#ifdef CONFIG_MEMCG
	set_active_memcg(old_memcg);
	mem_cgroup_put(memcg);
#endif
	return ret;
}


static int btf_field_cmp(const void *a, const void *b)
{
	const struct btf_field *f1 = a, *f2 = b;

	if (f1->offset < f2->offset)
		return -1;
	else if (f1->offset > f2->offset)
		return 1;
	return 0;
}

struct btf_field *btf_record_find(const struct btf_record *rec, u32 offset,
				  u32 field_mask)
{
	struct btf_field *field;

	if (IS_ERR_OR_NULL(rec) || !(rec->field_mask & field_mask))
		return NULL;
	field = bsearch(&offset, rec->fields, rec->cnt, sizeof(rec->fields[0]), btf_field_cmp);
	if (!field || !(field->type & field_mask))
		return NULL;
	return field;
}

void btf_record_free(struct btf_record *rec)
{
	int i;

	if (IS_ERR_OR_NULL(rec))
		return;
	for (i = 0; i < rec->cnt; i++) {
		switch (rec->fields[i].type) {
		case BPF_KPTR_UNREF:
		case BPF_KPTR_REF:
		case BPF_KPTR_PERCPU:
		case BPF_UPTR:
			if (rec->fields[i].kptr.module)
				module_put(rec->fields[i].kptr.module);
			if (btf_is_kernel(rec->fields[i].kptr.btf))
				btf_put(rec->fields[i].kptr.btf);
			break;
		case BPF_LIST_HEAD:
		case BPF_LIST_NODE:
		case BPF_RB_ROOT:
		case BPF_RB_NODE:
		case BPF_SPIN_LOCK:
		case BPF_RES_SPIN_LOCK:
		case BPF_TIMER:
		case BPF_REFCOUNT:
		case BPF_WORKQUEUE:
			/* Nothing to release */
			break;
		default:
			WARN_ON_ONCE(1);
			continue;
		}
	}
	kfree(rec);
}

void bpf_map_free_record(struct bpf_map *map)
{
	btf_record_free(map->record);
	map->record = NULL;
}

struct btf_record *btf_record_dup(const struct btf_record *rec)
{
	const struct btf_field *fields;
	struct btf_record *new_rec;
	int ret, size, i;

	if (IS_ERR_OR_NULL(rec))
		return NULL;
	size = struct_size(rec, fields, rec->cnt);
	new_rec = kmemdup(rec, size, GFP_KERNEL | __GFP_NOWARN);
	if (!new_rec)
		return ERR_PTR(-ENOMEM);
	/* Do a deep copy of the btf_record */
	fields = rec->fields;
	new_rec->cnt = 0;
	for (i = 0; i < rec->cnt; i++) {
		switch (fields[i].type) {
		case BPF_KPTR_UNREF:
		case BPF_KPTR_REF:
		case BPF_KPTR_PERCPU:
		case BPF_UPTR:
			if (btf_is_kernel(fields[i].kptr.btf))
				btf_get(fields[i].kptr.btf);
			if (fields[i].kptr.module && !try_module_get(fields[i].kptr.module)) {
				ret = -ENXIO;
				goto free;
			}
			break;
		case BPF_LIST_HEAD:
		case BPF_LIST_NODE:
		case BPF_RB_ROOT:
		case BPF_RB_NODE:
		case BPF_SPIN_LOCK:
		case BPF_RES_SPIN_LOCK:
		case BPF_TIMER:
		case BPF_REFCOUNT:
		case BPF_WORKQUEUE:
			/* Nothing to acquire */
			break;
		default:
			ret = -EFAULT;
			WARN_ON_ONCE(1);
			goto free;
		}
		new_rec->cnt++;
	}
	return new_rec;
free:
	btf_record_free(new_rec);
	return ERR_PTR(ret);
}

bool btf_record_equal(const struct btf_record *rec_a, const struct btf_record *rec_b)
{
	bool a_has_fields = !IS_ERR_OR_NULL(rec_a), b_has_fields = !IS_ERR_OR_NULL(rec_b);
	int size;

	if (!a_has_fields && !b_has_fields)
		return true;
	if (a_has_fields != b_has_fields)
		return false;
	if (rec_a->cnt != rec_b->cnt)
		return false;
	size = struct_size(rec_a, fields, rec_a->cnt);
	/* btf_parse_fields uses kzalloc to allocate a btf_record, so unused
	 * members are zeroed out. So memcmp is safe to do without worrying
	 * about padding/unused fields.
	 *
	 * While spin_lock, timer, and kptr have no relation to map BTF,
	 * list_head metadata is specific to map BTF, the btf and value_rec
	 * members in particular. btf is the map BTF, while value_rec points to
	 * btf_record in that map BTF.
	 *
	 * So while by default, we don't rely on the map BTF (which the records
	 * were parsed from) matching for both records, which is not backwards
	 * compatible, in case list_head is part of it, we implicitly rely on
	 * that by way of depending on memcmp succeeding for it.
	 */
	return !memcmp(rec_a, rec_b, size);
}

void bpf_obj_free_timer(const struct btf_record *rec, void *obj)
{
	if (WARN_ON_ONCE(!btf_record_has_field(rec, BPF_TIMER)))
		return;
	bpf_timer_cancel_and_free(obj + rec->timer_off);
}

void bpf_obj_free_workqueue(const struct btf_record *rec, void *obj)
{
	if (WARN_ON_ONCE(!btf_record_has_field(rec, BPF_WORKQUEUE)))
		return;
	bpf_wq_cancel_and_free(obj + rec->wq_off);
}

void bpf_obj_free_fields(const struct btf_record *rec, void *obj)
{
	const struct btf_field *fields;
	int i;

	if (IS_ERR_OR_NULL(rec))
		return;
	fields = rec->fields;
	for (i = 0; i < rec->cnt; i++) {
		struct btf_struct_meta *pointee_struct_meta;
		const struct btf_field *field = &fields[i];
		void *field_ptr = obj + field->offset;
		void *xchgd_field;

		switch (fields[i].type) {
		case BPF_SPIN_LOCK:
		case BPF_RES_SPIN_LOCK:
			break;
		case BPF_TIMER:
			bpf_timer_cancel_and_free(field_ptr);
			break;
		case BPF_WORKQUEUE:
			bpf_wq_cancel_and_free(field_ptr);
			break;
		case BPF_KPTR_UNREF:
			WRITE_ONCE(*(u64 *)field_ptr, 0);
			break;
		case BPF_KPTR_REF:
		case BPF_KPTR_PERCPU:
			xchgd_field = (void *)xchg((unsigned long *)field_ptr, 0);
			if (!xchgd_field)
				break;

			if (!btf_is_kernel(field->kptr.btf)) {
				pointee_struct_meta = btf_find_struct_meta(field->kptr.btf,
									   field->kptr.btf_id);
				__bpf_obj_drop_impl(xchgd_field, pointee_struct_meta ?
								 pointee_struct_meta->record : NULL,
								 fields[i].type == BPF_KPTR_PERCPU);
			} else {
				field->kptr.dtor(xchgd_field);
			}
			break;
		case BPF_UPTR:
			/* The caller ensured that no one is using the uptr */
			unpin_uptr_kaddr(*(void **)field_ptr);
			break;
		case BPF_LIST_HEAD:
			if (WARN_ON_ONCE(rec->spin_lock_off < 0))
				continue;
			bpf_list_head_free(field, field_ptr, obj + rec->spin_lock_off);
			break;
		case BPF_RB_ROOT:
			if (WARN_ON_ONCE(rec->spin_lock_off < 0))
				continue;
			bpf_rb_root_free(field, field_ptr, obj + rec->spin_lock_off);
			break;
		case BPF_LIST_NODE:
		case BPF_RB_NODE:
		case BPF_REFCOUNT:
			break;
		default:
			WARN_ON_ONCE(1);
			continue;
		}
	}
}

static void bpf_map_free(struct bpf_map *map)
{
	struct btf_record *rec = map->record;
	struct btf *btf = map->btf;

	/* implementation dependent freeing. Disabling migration to simplify
	 * the free of values or special fields allocated from bpf memory
	 * allocator.
	 */
	migrate_disable();
	map->ops->map_free(map);
	migrate_enable();

	/* Delay freeing of btf_record for maps, as map_free
	 * callback usually needs access to them. It is better to do it here
	 * than require each callback to do the free itself manually.
	 *
	 * Note that the btf_record stashed in map->inner_map_meta->record was
	 * already freed using the map_free callback for map in map case which
	 * eventually calls bpf_map_free_meta, since inner_map_meta is only a
	 * template bpf_map struct used during verification.
	 */
	btf_record_free(rec);
	/* Delay freeing of btf for maps, as map_free callback may need
	 * struct_meta info which will be freed with btf_put().
	 */
	btf_put(btf);
}

/* called from workqueue */
static void bpf_map_free_deferred(struct work_struct *work)
{
	struct bpf_map *map = container_of(work, struct bpf_map, work);

	security_bpf_map_free(map);
	bpf_map_release_memcg(map);
	bpf_map_free(map);
}

static void bpf_map_put_uref(struct bpf_map *map)
{
	if (atomic64_dec_and_test(&map->usercnt)) {
		if (map->ops->map_release_uref)
			map->ops->map_release_uref(map);
	}
}

static void bpf_map_free_in_work(struct bpf_map *map)
{
	INIT_WORK(&map->work, bpf_map_free_deferred);
	/* Avoid spawning kworkers, since they all might contend
	 * for the same mutex like slab_mutex.
	 */
	queue_work(system_unbound_wq, &map->work);
}

static void bpf_map_free_rcu_gp(struct rcu_head *rcu)
{
	bpf_map_free_in_work(container_of(rcu, struct bpf_map, rcu));
}

static void bpf_map_free_mult_rcu_gp(struct rcu_head *rcu)
{
	if (rcu_trace_implies_rcu_gp())
		bpf_map_free_rcu_gp(rcu);
	else
		call_rcu(rcu, bpf_map_free_rcu_gp);
}

/* decrement map refcnt and schedule it for freeing via workqueue
 * (underlying map implementation ops->map_free() might sleep)
 */
void bpf_map_put(struct bpf_map *map)
{
	if (atomic64_dec_and_test(&map->refcnt)) {
		/* bpf_map_free_id() must be called first */
		bpf_map_free_id(map);

		WARN_ON_ONCE(atomic64_read(&map->sleepable_refcnt));
		if (READ_ONCE(map->free_after_mult_rcu_gp))
			call_rcu_tasks_trace(&map->rcu, bpf_map_free_mult_rcu_gp);
		else if (READ_ONCE(map->free_after_rcu_gp))
			call_rcu(&map->rcu, bpf_map_free_rcu_gp);
		else
			bpf_map_free_in_work(map);
	}
}
EXPORT_SYMBOL_GPL(bpf_map_put);

void bpf_map_put_with_uref(struct bpf_map *map)
{
	bpf_map_put_uref(map);
	bpf_map_put(map);
}

static int bpf_map_release(struct inode *inode, struct file *filp)
{
	struct bpf_map *map = filp->private_data;

	if (map->ops->map_release)
		map->ops->map_release(map, filp);

	bpf_map_put_with_uref(map);
	return 0;
}

static fmode_t map_get_sys_perms(struct bpf_map *map, struct fd f)
{
	fmode_t mode = fd_file(f)->f_mode;

	/* Our file permissions may have been overridden by global
	 * map permissions facing syscall side.
	 */
	if (READ_ONCE(map->frozen))
		mode &= ~FMODE_CAN_WRITE;
	return mode;
}

#ifdef CONFIG_PROC_FS
/* Show the memory usage of a bpf map */
static u64 bpf_map_memory_usage(const struct bpf_map *map)
{
	return map->ops->map_mem_usage(map);
}

static void bpf_map_show_fdinfo(struct seq_file *m, struct file *filp)
{
	struct bpf_map *map = filp->private_data;
	u32 type = 0, jited = 0;

	if (map_type_contains_progs(map)) {
		spin_lock(&map->owner.lock);
		type  = map->owner.type;
		jited = map->owner.jited;
		spin_unlock(&map->owner.lock);
	}

	seq_printf(m,
		   "map_type:\t%u\n"
		   "key_size:\t%u\n"
		   "value_size:\t%u\n"
		   "max_entries:\t%u\n"
		   "map_flags:\t%#x\n"
		   "map_extra:\t%#llx\n"
		   "memlock:\t%llu\n"
		   "map_id:\t%u\n"
		   "frozen:\t%u\n",
		   map->map_type,
		   map->key_size,
		   map->value_size,
		   map->max_entries,
		   map->map_flags,
		   (unsigned long long)map->map_extra,
		   bpf_map_memory_usage(map),
		   map->id,
		   READ_ONCE(map->frozen));
	if (type) {
		seq_printf(m, "owner_prog_type:\t%u\n", type);
		seq_printf(m, "owner_jited:\t%u\n", jited);
	}
}
#endif

static ssize_t bpf_dummy_read(struct file *filp, char __user *buf, size_t siz,
			      loff_t *ppos)
{
	/* We need this handler such that alloc_file() enables
	 * f_mode with FMODE_CAN_READ.
	 */
	return -EINVAL;
}

static ssize_t bpf_dummy_write(struct file *filp, const char __user *buf,
			       size_t siz, loff_t *ppos)
{
	/* We need this handler such that alloc_file() enables
	 * f_mode with FMODE_CAN_WRITE.
	 */
	return -EINVAL;
}

/* called for any extra memory-mapped regions (except initial) */
static void bpf_map_mmap_open(struct vm_area_struct *vma)
{
	struct bpf_map *map = vma->vm_file->private_data;

	if (vma->vm_flags & VM_MAYWRITE)
		bpf_map_write_active_inc(map);
}

/* called for all unmapped memory region (including initial) */
static void bpf_map_mmap_close(struct vm_area_struct *vma)
{
	struct bpf_map *map = vma->vm_file->private_data;

	if (vma->vm_flags & VM_MAYWRITE)
		bpf_map_write_active_dec(map);
}

static const struct vm_operations_struct bpf_map_default_vmops = {
	.open		= bpf_map_mmap_open,
	.close		= bpf_map_mmap_close,
};

static int bpf_map_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct bpf_map *map = filp->private_data;
	int err = 0;

	if (!map->ops->map_mmap || !IS_ERR_OR_NULL(map->record))
		return -ENOTSUPP;

	if (!(vma->vm_flags & VM_SHARED))
		return -EINVAL;

	mutex_lock(&map->freeze_mutex);

	if (vma->vm_flags & VM_WRITE) {
		if (map->frozen) {
			err = -EPERM;
			goto out;
		}
		/* map is meant to be read-only, so do not allow mapping as
		 * writable, because it's possible to leak a writable page
		 * reference and allows user-space to still modify it after
		 * freezing, while verifier will assume contents do not change
		 */
		if (map->map_flags & BPF_F_RDONLY_PROG) {
			err = -EACCES;
			goto out;
		}
		bpf_map_write_active_inc(map);
	}
out:
	mutex_unlock(&map->freeze_mutex);
	if (err)
		return err;

	/* set default open/close callbacks */
	vma->vm_ops = &bpf_map_default_vmops;
	vma->vm_private_data = map;
	vm_flags_clear(vma, VM_MAYEXEC);
	/* If mapping is read-only, then disallow potentially re-mapping with
	 * PROT_WRITE by dropping VM_MAYWRITE flag. This VM_MAYWRITE clearing
	 * means that as far as BPF map's memory-mapped VMAs are concerned,
	 * VM_WRITE and VM_MAYWRITE and equivalent, if one of them is set,
	 * both should be set, so we can forget about VM_MAYWRITE and always
	 * check just VM_WRITE
	 */
	if (!(vma->vm_flags & VM_WRITE))
		vm_flags_clear(vma, VM_MAYWRITE);

	err = map->ops->map_mmap(map, vma);
	if (err) {
		if (vma->vm_flags & VM_WRITE)
			bpf_map_write_active_dec(map);
	}

	return err;
}

static __poll_t bpf_map_poll(struct file *filp, struct poll_table_struct *pts)
{
	struct bpf_map *map = filp->private_data;

	if (map->ops->map_poll)
		return map->ops->map_poll(map, filp, pts);

	return EPOLLERR;
}

static unsigned long bpf_get_unmapped_area(struct file *filp, unsigned long addr,
					   unsigned long len, unsigned long pgoff,
					   unsigned long flags)
{
	struct bpf_map *map = filp->private_data;

	if (map->ops->map_get_unmapped_area)
		return map->ops->map_get_unmapped_area(filp, addr, len, pgoff, flags);
#ifdef CONFIG_MMU
	return mm_get_unmapped_area(current->mm, filp, addr, len, pgoff, flags);
#else
	return addr;
#endif
}

const struct file_operations bpf_map_fops = {
#ifdef CONFIG_PROC_FS
	.show_fdinfo	= bpf_map_show_fdinfo,
#endif
	.release	= bpf_map_release,
	.read		= bpf_dummy_read,
	.write		= bpf_dummy_write,
	.mmap		= bpf_map_mmap,
	.poll		= bpf_map_poll,
	.get_unmapped_area = bpf_get_unmapped_area,
};

int bpf_map_new_fd(struct bpf_map *map, int flags)
{
	int ret;

	ret = security_bpf_map(map, OPEN_FMODE(flags));
	if (ret < 0)
		return ret;

	return anon_inode_getfd("bpf-map", &bpf_map_fops, map,
				flags | O_CLOEXEC);
}

int bpf_get_file_flag(int flags)
{
	if ((flags & BPF_F_RDONLY) && (flags & BPF_F_WRONLY))
		return -EINVAL;
	if (flags & BPF_F_RDONLY)
		return O_RDONLY;
	if (flags & BPF_F_WRONLY)
		return O_WRONLY;
	return O_RDWR;
}

/* helper macro to check that unused fields 'union bpf_attr' are zero */
#define CHECK_ATTR(CMD) \
	memchr_inv((void *) &attr->CMD##_LAST_FIELD + \
		   sizeof(attr->CMD##_LAST_FIELD), 0, \
		   sizeof(*attr) - \
		   offsetof(union bpf_attr, CMD##_LAST_FIELD) - \
		   sizeof(attr->CMD##_LAST_FIELD)) != NULL

/* dst and src must have at least "size" number of bytes.
 * Return strlen on success and < 0 on error.
 */
int bpf_obj_name_cpy(char *dst, const char *src, unsigned int size)
{
	const char *end = src + size;
	const char *orig_src = src;

	memset(dst, 0, size);
	/* Copy all isalnum(), '_' and '.' chars. */
	while (src < end && *src) {
		if (!isalnum(*src) &&
		    *src != '_' && *src != '.')
			return -EINVAL;
		*dst++ = *src++;
	}

	/* No '\0' found in "size" number of bytes */
	if (src == end)
		return -EINVAL;

	return src - orig_src;
}

int map_check_no_btf(const struct bpf_map *map,
		     const struct btf *btf,
		     const struct btf_type *key_type,
		     const struct btf_type *value_type)
{
	return -ENOTSUPP;
}

static int map_check_btf(struct bpf_map *map, struct bpf_token *token,
			 const struct btf *btf, u32 btf_key_id, u32 btf_value_id)
{
	const struct btf_type *key_type, *value_type;
	u32 key_size, value_size;
	int ret = 0;

	/* Some maps allow key to be unspecified. */
	if (btf_key_id) {
		key_type = btf_type_id_size(btf, &btf_key_id, &key_size);
		if (!key_type || key_size != map->key_size)
			return -EINVAL;
	} else {
		key_type = btf_type_by_id(btf, 0);
		if (!map->ops->map_check_btf)
			return -EINVAL;
	}

	value_type = btf_type_id_size(btf, &btf_value_id, &value_size);
	if (!value_type || value_size != map->value_size)
		return -EINVAL;

	map->record = btf_parse_fields(btf, value_type,
				       BPF_SPIN_LOCK | BPF_RES_SPIN_LOCK | BPF_TIMER | BPF_KPTR | BPF_LIST_HEAD |
				       BPF_RB_ROOT | BPF_REFCOUNT | BPF_WORKQUEUE | BPF_UPTR,
				       map->value_size);
	if (!IS_ERR_OR_NULL(map->record)) {
		int i;

		if (!bpf_token_capable(token, CAP_BPF)) {
			ret = -EPERM;
			goto free_map_tab;
		}
		if (map->map_flags & (BPF_F_RDONLY_PROG | BPF_F_WRONLY_PROG)) {
			ret = -EACCES;
			goto free_map_tab;
		}
		for (i = 0; i < sizeof(map->record->field_mask) * 8; i++) {
			switch (map->record->field_mask & (1 << i)) {
			case 0:
				continue;
			case BPF_SPIN_LOCK:
			case BPF_RES_SPIN_LOCK:
				if (map->map_type != BPF_MAP_TYPE_HASH &&
				    map->map_type != BPF_MAP_TYPE_ARRAY &&
				    map->map_type != BPF_MAP_TYPE_CGROUP_STORAGE &&
				    map->map_type != BPF_MAP_TYPE_SK_STORAGE &&
				    map->map_type != BPF_MAP_TYPE_INODE_STORAGE &&
				    map->map_type != BPF_MAP_TYPE_TASK_STORAGE &&
				    map->map_type != BPF_MAP_TYPE_CGRP_STORAGE) {
					ret = -EOPNOTSUPP;
					goto free_map_tab;
				}
				break;
			case BPF_TIMER:
			case BPF_WORKQUEUE:
				if (map->map_type != BPF_MAP_TYPE_HASH &&
				    map->map_type != BPF_MAP_TYPE_LRU_HASH &&
				    map->map_type != BPF_MAP_TYPE_ARRAY) {
					ret = -EOPNOTSUPP;
					goto free_map_tab;
				}
				break;
			case BPF_KPTR_UNREF:
			case BPF_KPTR_REF:
			case BPF_KPTR_PERCPU:
			case BPF_REFCOUNT:
				if (map->map_type != BPF_MAP_TYPE_HASH &&
				    map->map_type != BPF_MAP_TYPE_PERCPU_HASH &&
				    map->map_type != BPF_MAP_TYPE_LRU_HASH &&
				    map->map_type != BPF_MAP_TYPE_LRU_PERCPU_HASH &&
				    map->map_type != BPF_MAP_TYPE_ARRAY &&
				    map->map_type != BPF_MAP_TYPE_PERCPU_ARRAY &&
				    map->map_type != BPF_MAP_TYPE_SK_STORAGE &&
				    map->map_type != BPF_MAP_TYPE_INODE_STORAGE &&
				    map->map_type != BPF_MAP_TYPE_TASK_STORAGE &&
				    map->map_type != BPF_MAP_TYPE_CGRP_STORAGE) {
					ret = -EOPNOTSUPP;
					goto free_map_tab;
				}
				break;
			case BPF_UPTR:
				if (map->map_type != BPF_MAP_TYPE_TASK_STORAGE) {
					ret = -EOPNOTSUPP;
					goto free_map_tab;
				}
				break;
			case BPF_LIST_HEAD:
			case BPF_RB_ROOT:
				if (map->map_type != BPF_MAP_TYPE_HASH &&
				    map->map_type != BPF_MAP_TYPE_LRU_HASH &&
				    map->map_type != BPF_MAP_TYPE_ARRAY) {
					ret = -EOPNOTSUPP;
					goto free_map_tab;
				}
				break;
			default:
				/* Fail if map_type checks are missing for a field type */
				ret = -EOPNOTSUPP;
				goto free_map_tab;
			}
		}
	}

	ret = btf_check_and_fixup_fields(btf, map->record);
	if (ret < 0)
		goto free_map_tab;

	if (map->ops->map_check_btf) {
		ret = map->ops->map_check_btf(map, btf, key_type, value_type);
		if (ret < 0)
			goto free_map_tab;
	}

	return ret;
free_map_tab:
	bpf_map_free_record(map);
	return ret;
}

static bool bpf_net_capable(void)
{
	return capable(CAP_NET_ADMIN) || capable(CAP_SYS_ADMIN);
}

#define BPF_MAP_CREATE_LAST_FIELD map_token_fd
/* called via syscall */
static int map_create(union bpf_attr *attr, bool kernel)
{
	const struct bpf_map_ops *ops;
	struct bpf_token *token = NULL;
	int numa_node = bpf_map_attr_numa_node(attr);
	u32 map_type = attr->map_type;
	struct bpf_map *map;
	bool token_flag;
	int f_flags;
	int err;

	err = CHECK_ATTR(BPF_MAP_CREATE);
	if (err)
		return -EINVAL;

	/* check BPF_F_TOKEN_FD flag, remember if it's set, and then clear it
	 * to avoid per-map type checks tripping on unknown flag
	 */
	token_flag = attr->map_flags & BPF_F_TOKEN_FD;
	attr->map_flags &= ~BPF_F_TOKEN_FD;

	if (attr->btf_vmlinux_value_type_id) {
		if (attr->map_type != BPF_MAP_TYPE_STRUCT_OPS ||
		    attr->btf_key_type_id || attr->btf_value_type_id)
			return -EINVAL;
	} else if (attr->btf_key_type_id && !attr->btf_value_type_id) {
		return -EINVAL;
	}

	if (attr->map_type != BPF_MAP_TYPE_BLOOM_FILTER &&
	    attr->map_type != BPF_MAP_TYPE_ARENA &&
	    attr->map_extra != 0)
		return -EINVAL;

	f_flags = bpf_get_file_flag(attr->map_flags);
	if (f_flags < 0)
		return f_flags;

	if (numa_node != NUMA_NO_NODE &&
	    ((unsigned int)numa_node >= nr_node_ids ||
	     !node_online(numa_node)))
		return -EINVAL;

	/* find map type and init map: hashtable vs rbtree vs bloom vs ... */
	map_type = attr->map_type;
	if (map_type >= ARRAY_SIZE(bpf_map_types))
		return -EINVAL;
	map_type = array_index_nospec(map_type, ARRAY_SIZE(bpf_map_types));
	ops = bpf_map_types[map_type];
	if (!ops)
		return -EINVAL;

	if (ops->map_alloc_check) {
		err = ops->map_alloc_check(attr);
		if (err)
			return err;
	}
	if (attr->map_ifindex)
		ops = &bpf_map_offload_ops;
	if (!ops->map_mem_usage)
		return -EINVAL;

	if (token_flag) {
		token = bpf_token_get_from_fd(attr->map_token_fd);
		if (IS_ERR(token))
			return PTR_ERR(token);

		/* if current token doesn't grant map creation permissions,
		 * then we can't use this token, so ignore it and rely on
		 * system-wide capabilities checks
		 */
		if (!bpf_token_allow_cmd(token, BPF_MAP_CREATE) ||
		    !bpf_token_allow_map_type(token, attr->map_type)) {
			bpf_token_put(token);
			token = NULL;
		}
	}

	err = -EPERM;

	/* Intent here is for unprivileged_bpf_disabled to block BPF map
	 * creation for unprivileged users; other actions depend
	 * on fd availability and access to bpffs, so are dependent on
	 * object creation success. Even with unprivileged BPF disabled,
	 * capability checks are still carried out.
	 */
	if (sysctl_unprivileged_bpf_disabled && !bpf_token_capable(token, CAP_BPF))
		goto put_token;

	/* check privileged map type permissions */
	switch (map_type) {
	case BPF_MAP_TYPE_ARRAY:
	case BPF_MAP_TYPE_PERCPU_ARRAY:
	case BPF_MAP_TYPE_PROG_ARRAY:
	case BPF_MAP_TYPE_PERF_EVENT_ARRAY:
	case BPF_MAP_TYPE_CGROUP_ARRAY:
	case BPF_MAP_TYPE_ARRAY_OF_MAPS:
	case BPF_MAP_TYPE_HASH:
	case BPF_MAP_TYPE_PERCPU_HASH:
	case BPF_MAP_TYPE_HASH_OF_MAPS:
	case BPF_MAP_TYPE_RINGBUF:
	case BPF_MAP_TYPE_USER_RINGBUF:
	case BPF_MAP_TYPE_CGROUP_STORAGE:
	case BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE:
		/* unprivileged */
		break;
	case BPF_MAP_TYPE_SK_STORAGE:
	case BPF_MAP_TYPE_INODE_STORAGE:
	case BPF_MAP_TYPE_TASK_STORAGE:
	case BPF_MAP_TYPE_CGRP_STORAGE:
	case BPF_MAP_TYPE_BLOOM_FILTER:
	case BPF_MAP_TYPE_LPM_TRIE:
	case BPF_MAP_TYPE_REUSEPORT_SOCKARRAY:
	case BPF_MAP_TYPE_STACK_TRACE:
	case BPF_MAP_TYPE_QUEUE:
	case BPF_MAP_TYPE_STACK:
	case BPF_MAP_TYPE_LRU_HASH:
	case BPF_MAP_TYPE_LRU_PERCPU_HASH:
	case BPF_MAP_TYPE_STRUCT_OPS:
	case BPF_MAP_TYPE_CPUMAP:
	case BPF_MAP_TYPE_ARENA:
		if (!bpf_token_capable(token, CAP_BPF))
			goto put_token;
		break;
	case BPF_MAP_TYPE_SOCKMAP:
	case BPF_MAP_TYPE_SOCKHASH:
	case BPF_MAP_TYPE_DEVMAP:
	case BPF_MAP_TYPE_DEVMAP_HASH:
	case BPF_MAP_TYPE_XSKMAP:
		if (!bpf_token_capable(token, CAP_NET_ADMIN))
			goto put_token;
		break;
	default:
		WARN(1, "unsupported map type %d", map_type);
		goto put_token;
	}

	map = ops->map_alloc(attr);
	if (IS_ERR(map)) {
		err = PTR_ERR(map);
		goto put_token;
	}
	map->ops = ops;
	map->map_type = map_type;

	err = bpf_obj_name_cpy(map->name, attr->map_name,
			       sizeof(attr->map_name));
	if (err < 0)
		goto free_map;

	atomic64_set(&map->refcnt, 1);
	atomic64_set(&map->usercnt, 1);
	mutex_init(&map->freeze_mutex);
	spin_lock_init(&map->owner.lock);

	if (attr->btf_key_type_id || attr->btf_value_type_id ||
	    /* Even the map's value is a kernel's struct,
	     * the bpf_prog.o must have BTF to begin with
	     * to figure out the corresponding kernel's
	     * counter part.  Thus, attr->btf_fd has
	     * to be valid also.
	     */
	    attr->btf_vmlinux_value_type_id) {
		struct btf *btf;

		btf = btf_get_by_fd(attr->btf_fd);
		if (IS_ERR(btf)) {
			err = PTR_ERR(btf);
			goto free_map;
		}
		if (btf_is_kernel(btf)) {
			btf_put(btf);
			err = -EACCES;
			goto free_map;
		}
		map->btf = btf;

		if (attr->btf_value_type_id) {
			err = map_check_btf(map, token, btf, attr->btf_key_type_id,
					    attr->btf_value_type_id);
			if (err)
				goto free_map;
		}

		map->btf_key_type_id = attr->btf_key_type_id;
		map->btf_value_type_id = attr->btf_value_type_id;
		map->btf_vmlinux_value_type_id =
			attr->btf_vmlinux_value_type_id;
	}

	err = security_bpf_map_create(map, attr, token, kernel);
	if (err)
		goto free_map_sec;

	err = bpf_map_alloc_id(map);
	if (err)
		goto free_map_sec;

	bpf_map_save_memcg(map);
	bpf_token_put(token);

	err = bpf_map_new_fd(map, f_flags);
	if (err < 0) {
		/* failed to allocate fd.
		 * bpf_map_put_with_uref() is needed because the above
		 * bpf_map_alloc_id() has published the map
		 * to the userspace and the userspace may
		 * have refcnt-ed it through BPF_MAP_GET_FD_BY_ID.
		 */
		bpf_map_put_with_uref(map);
		return err;
	}

	return err;

free_map_sec:
	security_bpf_map_free(map);
free_map:
	bpf_map_free(map);
put_token:
	bpf_token_put(token);
	return err;
}

void bpf_map_inc(struct bpf_map *map)
{
	atomic64_inc(&map->refcnt);
}
EXPORT_SYMBOL_GPL(bpf_map_inc);

void bpf_map_inc_with_uref(struct bpf_map *map)
{
	atomic64_inc(&map->refcnt);
	atomic64_inc(&map->usercnt);
}
EXPORT_SYMBOL_GPL(bpf_map_inc_with_uref);

struct bpf_map *bpf_map_get(u32 ufd)
{
	CLASS(fd, f)(ufd);
	struct bpf_map *map = __bpf_map_get(f);

	if (!IS_ERR(map))
		bpf_map_inc(map);

	return map;
}
EXPORT_SYMBOL_NS(bpf_map_get, "BPF_INTERNAL");

struct bpf_map *bpf_map_get_with_uref(u32 ufd)
{
	CLASS(fd, f)(ufd);
	struct bpf_map *map = __bpf_map_get(f);

	if (!IS_ERR(map))
		bpf_map_inc_with_uref(map);

	return map;
}

/* map_idr_lock should have been held or the map should have been
 * protected by rcu read lock.
 */
struct bpf_map *__bpf_map_inc_not_zero(struct bpf_map *map, bool uref)
{
	int refold;

	refold = atomic64_fetch_add_unless(&map->refcnt, 1, 0);
	if (!refold)
		return ERR_PTR(-ENOENT);
	if (uref)
		atomic64_inc(&map->usercnt);

	return map;
}

struct bpf_map *bpf_map_inc_not_zero(struct bpf_map *map)
{
	lockdep_assert(rcu_read_lock_held());
	return __bpf_map_inc_not_zero(map, false);
}
EXPORT_SYMBOL_GPL(bpf_map_inc_not_zero);

int __weak bpf_stackmap_copy(struct bpf_map *map, void *key, void *value)
{
	return -ENOTSUPP;
}

static void *__bpf_copy_key(void __user *ukey, u64 key_size)
{
	if (key_size)
		return vmemdup_user(ukey, key_size);

	if (ukey)
		return ERR_PTR(-EINVAL);

	return NULL;
}

static void *___bpf_copy_key(bpfptr_t ukey, u64 key_size)
{
	if (key_size)
		return kvmemdup_bpfptr(ukey, key_size);

	if (!bpfptr_is_null(ukey))
		return ERR_PTR(-EINVAL);

	return NULL;
}

/* last field in 'union bpf_attr' used by this command */
#define BPF_MAP_LOOKUP_ELEM_LAST_FIELD flags

static int map_lookup_elem(union bpf_attr *attr)
{
	void __user *ukey = u64_to_user_ptr(attr->key);
	void __user *uvalue = u64_to_user_ptr(attr->value);
	struct bpf_map *map;
	void *key, *value;
	u32 value_size;
	int err;

	if (CHECK_ATTR(BPF_MAP_LOOKUP_ELEM))
		return -EINVAL;

	if (attr->flags & ~BPF_F_LOCK)
		return -EINVAL;

	CLASS(fd, f)(attr->map_fd);
	map = __bpf_map_get(f);
	if (IS_ERR(map))
		return PTR_ERR(map);
	if (!(map_get_sys_perms(map, f) & FMODE_CAN_READ))
		return -EPERM;

	if ((attr->flags & BPF_F_LOCK) &&
	    !btf_record_has_field(map->record, BPF_SPIN_LOCK))
		return -EINVAL;

	key = __bpf_copy_key(ukey, map->key_size);
	if (IS_ERR(key))
		return PTR_ERR(key);

	value_size = bpf_map_value_size(map);

	err = -ENOMEM;
	value = kvmalloc(value_size, GFP_USER | __GFP_NOWARN);
	if (!value)
		goto free_key;

	if (map->map_type == BPF_MAP_TYPE_BLOOM_FILTER) {
		if (copy_from_user(value, uvalue, value_size))
			err = -EFAULT;
		else
			err = bpf_map_copy_value(map, key, value, attr->flags);
		goto free_value;
	}

	err = bpf_map_copy_value(map, key, value, attr->flags);
	if (err)
		goto free_value;

	err = -EFAULT;
	if (copy_to_user(uvalue, value, value_size) != 0)
		goto free_value;

	err = 0;

free_value:
	kvfree(value);
free_key:
	kvfree(key);
	return err;
}


#define BPF_MAP_UPDATE_ELEM_LAST_FIELD flags

static int map_update_elem(union bpf_attr *attr, bpfptr_t uattr)
{
	bpfptr_t ukey = make_bpfptr(attr->key, uattr.is_kernel);
	bpfptr_t uvalue = make_bpfptr(attr->value, uattr.is_kernel);
	struct bpf_map *map;
	void *key, *value;
	u32 value_size;
	int err;

	if (CHECK_ATTR(BPF_MAP_UPDATE_ELEM))
		return -EINVAL;

	CLASS(fd, f)(attr->map_fd);
	map = __bpf_map_get(f);
	if (IS_ERR(map))
		return PTR_ERR(map);
	bpf_map_write_active_inc(map);
	if (!(map_get_sys_perms(map, f) & FMODE_CAN_WRITE)) {
		err = -EPERM;
		goto err_put;
	}

	if ((attr->flags & BPF_F_LOCK) &&
	    !btf_record_has_field(map->record, BPF_SPIN_LOCK)) {
		err = -EINVAL;
		goto err_put;
	}

	key = ___bpf_copy_key(ukey, map->key_size);
	if (IS_ERR(key)) {
		err = PTR_ERR(key);
		goto err_put;
	}

	value_size = bpf_map_value_size(map);
	value = kvmemdup_bpfptr(uvalue, value_size);
	if (IS_ERR(value)) {
		err = PTR_ERR(value);
		goto free_key;
	}

	err = bpf_map_update_value(map, fd_file(f), key, value, attr->flags);
	if (!err)
		maybe_wait_bpf_programs(map);

	kvfree(value);
free_key:
	kvfree(key);
err_put:
	bpf_map_write_active_dec(map);
	return err;
}

#define BPF_MAP_DELETE_ELEM_LAST_FIELD key

static int map_delete_elem(union bpf_attr *attr, bpfptr_t uattr)
{
	bpfptr_t ukey = make_bpfptr(attr->key, uattr.is_kernel);
	struct bpf_map *map;
	void *key;
	int err;

	if (CHECK_ATTR(BPF_MAP_DELETE_ELEM))
		return -EINVAL;

	CLASS(fd, f)(attr->map_fd);
	map = __bpf_map_get(f);
	if (IS_ERR(map))
		return PTR_ERR(map);
	bpf_map_write_active_inc(map);
	if (!(map_get_sys_perms(map, f) & FMODE_CAN_WRITE)) {
		err = -EPERM;
		goto err_put;
	}

	key = ___bpf_copy_key(ukey, map->key_size);
	if (IS_ERR(key)) {
		err = PTR_ERR(key);
		goto err_put;
	}

	if (bpf_map_is_offloaded(map)) {
		err = bpf_map_offload_delete_elem(map, key);
		goto out;
	} else if (IS_FD_PROG_ARRAY(map) ||
		   map->map_type == BPF_MAP_TYPE_STRUCT_OPS) {
		/* These maps require sleepable context */
		err = map->ops->map_delete_elem(map, key);
		goto out;
	}

	bpf_disable_instrumentation();
	rcu_read_lock();
	err = map->ops->map_delete_elem(map, key);
	rcu_read_unlock();
	bpf_enable_instrumentation();
	if (!err)
		maybe_wait_bpf_programs(map);
out:
	kvfree(key);
err_put:
	bpf_map_write_active_dec(map);
	return err;
}

/* last field in 'union bpf_attr' used by this command */
#define BPF_MAP_GET_NEXT_KEY_LAST_FIELD next_key

static int map_get_next_key(union bpf_attr *attr)
{
	void __user *ukey = u64_to_user_ptr(attr->key);
	void __user *unext_key = u64_to_user_ptr(attr->next_key);
	struct bpf_map *map;
	void *key, *next_key;
	int err;

	if (CHECK_ATTR(BPF_MAP_GET_NEXT_KEY))
		return -EINVAL;

	CLASS(fd, f)(attr->map_fd);
	map = __bpf_map_get(f);
	if (IS_ERR(map))
		return PTR_ERR(map);
	if (!(map_get_sys_perms(map, f) & FMODE_CAN_READ))
		return -EPERM;

	if (ukey) {
		key = __bpf_copy_key(ukey, map->key_size);
		if (IS_ERR(key))
			return PTR_ERR(key);
	} else {
		key = NULL;
	}

	err = -ENOMEM;
	next_key = kvmalloc(map->key_size, GFP_USER);
	if (!next_key)
		goto free_key;

	if (bpf_map_is_offloaded(map)) {
		err = bpf_map_offload_get_next_key(map, key, next_key);
		goto out;
	}

	rcu_read_lock();
	err = map->ops->map_get_next_key(map, key, next_key);
	rcu_read_unlock();
out:
	if (err)
		goto free_next_key;

	err = -EFAULT;
	if (copy_to_user(unext_key, next_key, map->key_size) != 0)
		goto free_next_key;

	err = 0;

free_next_key:
	kvfree(next_key);
free_key:
	kvfree(key);
	return err;
}

int generic_map_delete_batch(struct bpf_map *map,
			     const union bpf_attr *attr,
			     union bpf_attr __user *uattr)
{
	void __user *keys = u64_to_user_ptr(attr->batch.keys);
	u32 cp, max_count;
	int err = 0;
	void *key;

	if (attr->batch.elem_flags & ~BPF_F_LOCK)
		return -EINVAL;

	if ((attr->batch.elem_flags & BPF_F_LOCK) &&
	    !btf_record_has_field(map->record, BPF_SPIN_LOCK)) {
		return -EINVAL;
	}

	max_count = attr->batch.count;
	if (!max_count)
		return 0;

	if (put_user(0, &uattr->batch.count))
		return -EFAULT;

	key = kvmalloc(map->key_size, GFP_USER | __GFP_NOWARN);
	if (!key)
		return -ENOMEM;

	for (cp = 0; cp < max_count; cp++) {
		err = -EFAULT;
		if (copy_from_user(key, keys + cp * map->key_size,
				   map->key_size))
			break;

		if (bpf_map_is_offloaded(map)) {
			err = bpf_map_offload_delete_elem(map, key);
			break;
		}

		bpf_disable_instrumentation();
		rcu_read_lock();
		err = map->ops->map_delete_elem(map, key);
		rcu_read_unlock();
		bpf_enable_instrumentation();
		if (err)
			break;
		cond_resched();
	}
	if (copy_to_user(&uattr->batch.count, &cp, sizeof(cp)))
		err = -EFAULT;

	kvfree(key);

	return err;
}

int generic_map_update_batch(struct bpf_map *map, struct file *map_file,
			     const union bpf_attr *attr,
			     union bpf_attr __user *uattr)
{
	void __user *values = u64_to_user_ptr(attr->batch.values);
	void __user *keys = u64_to_user_ptr(attr->batch.keys);
	u32 value_size, cp, max_count;
	void *key, *value;
	int err = 0;

	if (attr->batch.elem_flags & ~BPF_F_LOCK)
		return -EINVAL;

	if ((attr->batch.elem_flags & BPF_F_LOCK) &&
	    !btf_record_has_field(map->record, BPF_SPIN_LOCK)) {
		return -EINVAL;
	}

	value_size = bpf_map_value_size(map);

	max_count = attr->batch.count;
	if (!max_count)
		return 0;

	if (put_user(0, &uattr->batch.count))
		return -EFAULT;

	key = kvmalloc(map->key_size, GFP_USER | __GFP_NOWARN);
	if (!key)
		return -ENOMEM;

	value = kvmalloc(value_size, GFP_USER | __GFP_NOWARN);
	if (!value) {
		kvfree(key);
		return -ENOMEM;
	}

	for (cp = 0; cp < max_count; cp++) {
		err = -EFAULT;
		if (copy_from_user(key, keys + cp * map->key_size,
		    map->key_size) ||
		    copy_from_user(value, values + cp * value_size, value_size))
			break;

		err = bpf_map_update_value(map, map_file, key, value,
					   attr->batch.elem_flags);

		if (err)
			break;
		cond_resched();
	}

	if (copy_to_user(&uattr->batch.count, &cp, sizeof(cp)))
		err = -EFAULT;

	kvfree(value);
	kvfree(key);

	return err;
}

int generic_map_lookup_batch(struct bpf_map *map,
				    const union bpf_attr *attr,
				    union bpf_attr __user *uattr)
{
	void __user *uobatch = u64_to_user_ptr(attr->batch.out_batch);
	void __user *ubatch = u64_to_user_ptr(attr->batch.in_batch);
	void __user *values = u64_to_user_ptr(attr->batch.values);
	void __user *keys = u64_to_user_ptr(attr->batch.keys);
	void *buf, *buf_prevkey, *prev_key, *key, *value;
	u32 value_size, cp, max_count;
	int err;

	if (attr->batch.elem_flags & ~BPF_F_LOCK)
		return -EINVAL;

	if ((attr->batch.elem_flags & BPF_F_LOCK) &&
	    !btf_record_has_field(map->record, BPF_SPIN_LOCK))
		return -EINVAL;

	value_size = bpf_map_value_size(map);

	max_count = attr->batch.count;
	if (!max_count)
		return 0;

	if (put_user(0, &uattr->batch.count))
		return -EFAULT;

	buf_prevkey = kvmalloc(map->key_size, GFP_USER | __GFP_NOWARN);
	if (!buf_prevkey)
		return -ENOMEM;

	buf = kvmalloc(map->key_size + value_size, GFP_USER | __GFP_NOWARN);
	if (!buf) {
		kvfree(buf_prevkey);
		return -ENOMEM;
	}

	err = -EFAULT;
	prev_key = NULL;
	if (ubatch && copy_from_user(buf_prevkey, ubatch, map->key_size))
		goto free_buf;
	key = buf;
	value = key + map->key_size;
	if (ubatch)
		prev_key = buf_prevkey;

	for (cp = 0; cp < max_count;) {
		rcu_read_lock();
		err = map->ops->map_get_next_key(map, prev_key, key);
		rcu_read_unlock();
		if (err)
			break;
		err = bpf_map_copy_value(map, key, value,
					 attr->batch.elem_flags);

		if (err == -ENOENT)
			goto next_key;

		if (err)
			goto free_buf;

		if (copy_to_user(keys + cp * map->key_size, key,
				 map->key_size)) {
			err = -EFAULT;
			goto free_buf;
		}
		if (copy_to_user(values + cp * value_size, value, value_size)) {
			err = -EFAULT;
			goto free_buf;
		}

		cp++;
next_key:
		if (!prev_key)
			prev_key = buf_prevkey;

		swap(prev_key, key);
		cond_resched();
	}

	if (err == -EFAULT)
		goto free_buf;

	if ((copy_to_user(&uattr->batch.count, &cp, sizeof(cp)) ||
		    (cp && copy_to_user(uobatch, prev_key, map->key_size))))
		err = -EFAULT;

free_buf:
	kvfree(buf_prevkey);
	kvfree(buf);
	return err;
}

#define BPF_MAP_LOOKUP_AND_DELETE_ELEM_LAST_FIELD flags

static int map_lookup_and_delete_elem(union bpf_attr *attr)
{
	void __user *ukey = u64_to_user_ptr(attr->key);
	void __user *uvalue = u64_to_user_ptr(attr->value);
	struct bpf_map *map;
	void *key, *value;
	u32 value_size;
	int err;

	if (CHECK_ATTR(BPF_MAP_LOOKUP_AND_DELETE_ELEM))
		return -EINVAL;

	if (attr->flags & ~BPF_F_LOCK)
		return -EINVAL;

	CLASS(fd, f)(attr->map_fd);
	map = __bpf_map_get(f);
	if (IS_ERR(map))
		return PTR_ERR(map);
	bpf_map_write_active_inc(map);
	if (!(map_get_sys_perms(map, f) & FMODE_CAN_READ) ||
	    !(map_get_sys_perms(map, f) & FMODE_CAN_WRITE)) {
		err = -EPERM;
		goto err_put;
	}

	if (attr->flags &&
	    (map->map_type == BPF_MAP_TYPE_QUEUE ||
	     map->map_type == BPF_MAP_TYPE_STACK)) {
		err = -EINVAL;
		goto err_put;
	}

	if ((attr->flags & BPF_F_LOCK) &&
	    !btf_record_has_field(map->record, BPF_SPIN_LOCK)) {
		err = -EINVAL;
		goto err_put;
	}

	key = __bpf_copy_key(ukey, map->key_size);
	if (IS_ERR(key)) {
		err = PTR_ERR(key);
		goto err_put;
	}

	value_size = bpf_map_value_size(map);

	err = -ENOMEM;
	value = kvmalloc(value_size, GFP_USER | __GFP_NOWARN);
	if (!value)
		goto free_key;

	err = -ENOTSUPP;
	if (map->map_type == BPF_MAP_TYPE_QUEUE ||
	    map->map_type == BPF_MAP_TYPE_STACK) {
		err = map->ops->map_pop_elem(map, value);
	} else if (map->map_type == BPF_MAP_TYPE_HASH ||
		   map->map_type == BPF_MAP_TYPE_PERCPU_HASH ||
		   map->map_type == BPF_MAP_TYPE_LRU_HASH ||
		   map->map_type == BPF_MAP_TYPE_LRU_PERCPU_HASH) {
		if (!bpf_map_is_offloaded(map)) {
			bpf_disable_instrumentation();
			rcu_read_lock();
			err = map->ops->map_lookup_and_delete_elem(map, key, value, attr->flags);
			rcu_read_unlock();
			bpf_enable_instrumentation();
		}
	}

	if (err)
		goto free_value;

	if (copy_to_user(uvalue, value, value_size) != 0) {
		err = -EFAULT;
		goto free_value;
	}

	err = 0;

free_value:
	kvfree(value);
free_key:
	kvfree(key);
err_put:
	bpf_map_write_active_dec(map);
	return err;
}

#define BPF_MAP_FREEZE_LAST_FIELD map_fd

static int map_freeze(const union bpf_attr *attr)
{
	int err = 0;
	struct bpf_map *map;

	if (CHECK_ATTR(BPF_MAP_FREEZE))
		return -EINVAL;

	CLASS(fd, f)(attr->map_fd);
	map = __bpf_map_get(f);
	if (IS_ERR(map))
		return PTR_ERR(map);

	if (map->map_type == BPF_MAP_TYPE_STRUCT_OPS || !IS_ERR_OR_NULL(map->record))
		return -ENOTSUPP;

	if (!(map_get_sys_perms(map, f) & FMODE_CAN_WRITE))
		return -EPERM;

	mutex_lock(&map->freeze_mutex);
	if (bpf_map_write_active(map)) {
		err = -EBUSY;
		goto err_put;
	}
	if (READ_ONCE(map->frozen)) {
		err = -EBUSY;
		goto err_put;
	}

	WRITE_ONCE(map->frozen, true);
err_put:
	mutex_unlock(&map->freeze_mutex);
	return err;
}

static const struct bpf_prog_ops * const bpf_prog_types[] = {
#define BPF_PROG_TYPE(_id, _name, prog_ctx_type, kern_ctx_type) \
	[_id] = & _name ## _prog_ops,
#define BPF_MAP_TYPE(_id, _ops)
#define BPF_LINK_TYPE(_id, _name)
#include <linux/bpf_types.h>
#undef BPF_PROG_TYPE
#undef BPF_MAP_TYPE
#undef BPF_LINK_TYPE
};

static int find_prog_type(enum bpf_prog_type type, struct bpf_prog *prog)
{
	const struct bpf_prog_ops *ops;

	if (type >= ARRAY_SIZE(bpf_prog_types))
		return -EINVAL;
	type = array_index_nospec(type, ARRAY_SIZE(bpf_prog_types));
	ops = bpf_prog_types[type];
	if (!ops)
		return -EINVAL;

	if (!bpf_prog_is_offloaded(prog->aux))
		prog->aux->ops = ops;
	else
		prog->aux->ops = &bpf_offload_prog_ops;
	prog->type = type;
	return 0;
}

enum bpf_audit {
	BPF_AUDIT_LOAD,
	BPF_AUDIT_UNLOAD,
	BPF_AUDIT_MAX,
};

static const char * const bpf_audit_str[BPF_AUDIT_MAX] = {
	[BPF_AUDIT_LOAD]   = "LOAD",
	[BPF_AUDIT_UNLOAD] = "UNLOAD",
};

static void bpf_audit_prog(const struct bpf_prog *prog, unsigned int op)
{
	struct audit_context *ctx = NULL;
	struct audit_buffer *ab;

	if (WARN_ON_ONCE(op >= BPF_AUDIT_MAX))
		return;
	if (audit_enabled == AUDIT_OFF)
		return;
	if (!in_irq() && !irqs_disabled())
		ctx = audit_context();
	ab = audit_log_start(ctx, GFP_ATOMIC, AUDIT_BPF);
	if (unlikely(!ab))
		return;
	audit_log_format(ab, "prog-id=%u op=%s",
			 prog->aux->id, bpf_audit_str[op]);
	audit_log_end(ab);
}

static int bpf_prog_alloc_id(struct bpf_prog *prog)
{
	int id;

	idr_preload(GFP_KERNEL);
	spin_lock_bh(&prog_idr_lock);
	id = idr_alloc_cyclic(&prog_idr, prog, 1, INT_MAX, GFP_ATOMIC);
	if (id > 0)
		prog->aux->id = id;
	spin_unlock_bh(&prog_idr_lock);
	idr_preload_end();

	/* id is in [1, INT_MAX) */
	if (WARN_ON_ONCE(!id))
		return -ENOSPC;

	return id > 0 ? 0 : id;
}

void bpf_prog_free_id(struct bpf_prog *prog)
{
	unsigned long flags;

	/* cBPF to eBPF migrations are currently not in the idr store.
	 * Offloaded programs are removed from the store when their device
	 * disappears - even if someone grabs an fd to them they are unusable,
	 * simply waiting for refcnt to drop to be freed.
	 */
	if (!prog->aux->id)
		return;

	spin_lock_irqsave(&prog_idr_lock, flags);
	idr_remove(&prog_idr, prog->aux->id);
	prog->aux->id = 0;
	spin_unlock_irqrestore(&prog_idr_lock, flags);
}

static void __bpf_prog_put_rcu(struct rcu_head *rcu)
{
	struct bpf_prog_aux *aux = container_of(rcu, struct bpf_prog_aux, rcu);

	kvfree(aux->func_info);
	kfree(aux->func_info_aux);
	free_uid(aux->user);
	security_bpf_prog_free(aux->prog);
	bpf_prog_free(aux->prog);
}

static void __bpf_prog_put_noref(struct bpf_prog *prog, bool deferred)
{
	bpf_prog_kallsyms_del_all(prog);
	btf_put(prog->aux->btf);
	module_put(prog->aux->mod);
	kvfree(prog->aux->jited_linfo);
	kvfree(prog->aux->linfo);
	kfree(prog->aux->kfunc_tab);
	kfree(prog->aux->ctx_arg_info);
	if (prog->aux->attach_btf)
		btf_put(prog->aux->attach_btf);

	if (deferred) {
		if (prog->sleepable)
			call_rcu_tasks_trace(&prog->aux->rcu, __bpf_prog_put_rcu);
		else
			call_rcu(&prog->aux->rcu, __bpf_prog_put_rcu);
	} else {
		__bpf_prog_put_rcu(&prog->aux->rcu);
	}
}

static void bpf_prog_put_deferred(struct work_struct *work)
{
	struct bpf_prog_aux *aux;
	struct bpf_prog *prog;

	aux = container_of(work, struct bpf_prog_aux, work);
	prog = aux->prog;
	perf_event_bpf_event(prog, PERF_BPF_EVENT_PROG_UNLOAD, 0);
	bpf_audit_prog(prog, BPF_AUDIT_UNLOAD);
	bpf_prog_free_id(prog);
	__bpf_prog_put_noref(prog, true);
}

static void __bpf_prog_put(struct bpf_prog *prog)
{
	struct bpf_prog_aux *aux = prog->aux;

	if (atomic64_dec_and_test(&aux->refcnt)) {
		if (in_irq() || irqs_disabled()) {
			INIT_WORK(&aux->work, bpf_prog_put_deferred);
			schedule_work(&aux->work);
		} else {
			bpf_prog_put_deferred(&aux->work);
		}
	}
}

void bpf_prog_put(struct bpf_prog *prog)
{
	__bpf_prog_put(prog);
}
EXPORT_SYMBOL_GPL(bpf_prog_put);

static int bpf_prog_release(struct inode *inode, struct file *filp)
{
	struct bpf_prog *prog = filp->private_data;

	bpf_prog_put(prog);
	return 0;
}

struct bpf_prog_kstats {
	u64 nsecs;
	u64 cnt;
	u64 misses;
};

void notrace bpf_prog_inc_misses_counter(struct bpf_prog *prog)
{
	struct bpf_prog_stats *stats;
	unsigned int flags;

	stats = this_cpu_ptr(prog->stats);
	flags = u64_stats_update_begin_irqsave(&stats->syncp);
	u64_stats_inc(&stats->misses);
	u64_stats_update_end_irqrestore(&stats->syncp, flags);
}

static void bpf_prog_get_stats(const struct bpf_prog *prog,
			       struct bpf_prog_kstats *stats)
{
	u64 nsecs = 0, cnt = 0, misses = 0;
	int cpu;

	for_each_possible_cpu(cpu) {
		const struct bpf_prog_stats *st;
		unsigned int start;
		u64 tnsecs, tcnt, tmisses;

		st = per_cpu_ptr(prog->stats, cpu);
		do {
			start = u64_stats_fetch_begin(&st->syncp);
			tnsecs = u64_stats_read(&st->nsecs);
			tcnt = u64_stats_read(&st->cnt);
			tmisses = u64_stats_read(&st->misses);
		} while (u64_stats_fetch_retry(&st->syncp, start));
		nsecs += tnsecs;
		cnt += tcnt;
		misses += tmisses;
	}
	stats->nsecs = nsecs;
	stats->cnt = cnt;
	stats->misses = misses;
}

#ifdef CONFIG_PROC_FS
static void bpf_prog_show_fdinfo(struct seq_file *m, struct file *filp)
{
	const struct bpf_prog *prog = filp->private_data;
	char prog_tag[sizeof(prog->tag) * 2 + 1] = { };
	struct bpf_prog_kstats stats;

	bpf_prog_get_stats(prog, &stats);
	bin2hex(prog_tag, prog->tag, sizeof(prog->tag));
	seq_printf(m,
		   "prog_type:\t%u\n"
		   "prog_jited:\t%u\n"
		   "prog_tag:\t%s\n"
		   "memlock:\t%llu\n"
		   "prog_id:\t%u\n"
		   "run_time_ns:\t%llu\n"
		   "run_cnt:\t%llu\n"
		   "recursion_misses:\t%llu\n"
		   "verified_insns:\t%u\n",
		   prog->type,
		   prog->jited,
		   prog_tag,
		   prog->pages * 1ULL << PAGE_SHIFT,
		   prog->aux->id,
		   stats.nsecs,
		   stats.cnt,
		   stats.misses,
		   prog->aux->verified_insns);
}
#endif

const struct file_operations bpf_prog_fops = {
#ifdef CONFIG_PROC_FS
	.show_fdinfo	= bpf_prog_show_fdinfo,
#endif
	.release	= bpf_prog_release,
	.read		= bpf_dummy_read,
	.write		= bpf_dummy_write,
};

int bpf_prog_new_fd(struct bpf_prog *prog)
{
	int ret;

	ret = security_bpf_prog(prog);
	if (ret < 0)
		return ret;

	return anon_inode_getfd("bpf-prog", &bpf_prog_fops, prog,
				O_RDWR | O_CLOEXEC);
}

void bpf_prog_add(struct bpf_prog *prog, int i)
{
	atomic64_add(i, &prog->aux->refcnt);
}
EXPORT_SYMBOL_GPL(bpf_prog_add);

void bpf_prog_sub(struct bpf_prog *prog, int i)
{
	/* Only to be used for undoing previous bpf_prog_add() in some
	 * error path. We still know that another entity in our call
	 * path holds a reference to the program, thus atomic_sub() can
	 * be safely used in such cases!
	 */
	WARN_ON(atomic64_sub_return(i, &prog->aux->refcnt) == 0);
}
EXPORT_SYMBOL_GPL(bpf_prog_sub);

void bpf_prog_inc(struct bpf_prog *prog)
{
	atomic64_inc(&prog->aux->refcnt);
}
EXPORT_SYMBOL_GPL(bpf_prog_inc);

/* prog_idr_lock should have been held */
struct bpf_prog *bpf_prog_inc_not_zero(struct bpf_prog *prog)
{
	int refold;

	refold = atomic64_fetch_add_unless(&prog->aux->refcnt, 1, 0);

	if (!refold)
		return ERR_PTR(-ENOENT);

	return prog;
}
EXPORT_SYMBOL_GPL(bpf_prog_inc_not_zero);

bool bpf_prog_get_ok(struct bpf_prog *prog,
			    enum bpf_prog_type *attach_type, bool attach_drv)
{
	/* not an attachment, just a refcount inc, always allow */
	if (!attach_type)
		return true;

	if (prog->type != *attach_type)
		return false;
	if (bpf_prog_is_offloaded(prog->aux) && !attach_drv)
		return false;

	return true;
}

static struct bpf_prog *__bpf_prog_get(u32 ufd, enum bpf_prog_type *attach_type,
				       bool attach_drv)
{
	CLASS(fd, f)(ufd);
	struct bpf_prog *prog;

	if (fd_empty(f))
		return ERR_PTR(-EBADF);
	if (fd_file(f)->f_op != &bpf_prog_fops)
		return ERR_PTR(-EINVAL);

	prog = fd_file(f)->private_data;
	if (!bpf_prog_get_ok(prog, attach_type, attach_drv))
		return ERR_PTR(-EINVAL);

	bpf_prog_inc(prog);
	return prog;
}

struct bpf_prog *bpf_prog_get(u32 ufd)
{
	return __bpf_prog_get(ufd, NULL, false);
}

struct bpf_prog *bpf_prog_get_type_dev(u32 ufd, enum bpf_prog_type type,
				       bool attach_drv)
{
	return __bpf_prog_get(ufd, &type, attach_drv);
}
EXPORT_SYMBOL_GPL(bpf_prog_get_type_dev);

/* Initially all BPF programs could be loaded w/o specifying
 * expected_attach_type. Later for some of them specifying expected_attach_type
 * at load time became required so that program could be validated properly.
 * Programs of types that are allowed to be loaded both w/ and w/o (for
 * backward compatibility) expected_attach_type, should have the default attach
 * type assigned to expected_attach_type for the latter case, so that it can be
 * validated later at attach time.
 *
 * bpf_prog_load_fixup_attach_type() sets expected_attach_type in @attr if
 * prog type requires it but has some attach types that have to be backward
 * compatible.
 */
static void bpf_prog_load_fixup_attach_type(union bpf_attr *attr)
{
	switch (attr->prog_type) {
	case BPF_PROG_TYPE_CGROUP_SOCK:
		/* Unfortunately BPF_ATTACH_TYPE_UNSPEC enumeration doesn't
		 * exist so checking for non-zero is the way to go here.
		 */
		if (!attr->expected_attach_type)
			attr->expected_attach_type =
				BPF_CGROUP_INET_SOCK_CREATE;
		break;
	case BPF_PROG_TYPE_SK_REUSEPORT:
		if (!attr->expected_attach_type)
			attr->expected_attach_type =
				BPF_SK_REUSEPORT_SELECT;
		break;
	}
}

static int
bpf_prog_load_check_attach(enum bpf_prog_type prog_type,
			   enum bpf_attach_type expected_attach_type,
			   struct btf *attach_btf, u32 btf_id,
			   struct bpf_prog *dst_prog)
{
	if (btf_id) {
		if (btf_id > BTF_MAX_TYPE)
			return -EINVAL;

		if (!attach_btf && !dst_prog)
			return -EINVAL;

		switch (prog_type) {
		case BPF_PROG_TYPE_TRACING:
		case BPF_PROG_TYPE_LSM:
		case BPF_PROG_TYPE_STRUCT_OPS:
		case BPF_PROG_TYPE_EXT:
			break;
		default:
			return -EINVAL;
		}
	}

	if (attach_btf && (!btf_id || dst_prog))
		return -EINVAL;

	if (dst_prog && prog_type != BPF_PROG_TYPE_TRACING &&
	    prog_type != BPF_PROG_TYPE_EXT)
		return -EINVAL;

	switch (prog_type) {
	case BPF_PROG_TYPE_CGROUP_SOCK:
		switch (expected_attach_type) {
		case BPF_CGROUP_INET_SOCK_CREATE:
		case BPF_CGROUP_INET_SOCK_RELEASE:
		case BPF_CGROUP_INET4_POST_BIND:
		case BPF_CGROUP_INET6_POST_BIND:
			return 0;
		default:
			return -EINVAL;
		}
	case BPF_PROG_TYPE_CGROUP_SOCK_ADDR:
		switch (expected_attach_type) {
		case BPF_CGROUP_INET4_BIND:
		case BPF_CGROUP_INET6_BIND:
		case BPF_CGROUP_INET4_CONNECT:
		case BPF_CGROUP_INET6_CONNECT:
		case BPF_CGROUP_UNIX_CONNECT:
		case BPF_CGROUP_INET4_GETPEERNAME:
		case BPF_CGROUP_INET6_GETPEERNAME:
		case BPF_CGROUP_UNIX_GETPEERNAME:
		case BPF_CGROUP_INET4_GETSOCKNAME:
		case BPF_CGROUP_INET6_GETSOCKNAME:
		case BPF_CGROUP_UNIX_GETSOCKNAME:
		case BPF_CGROUP_UDP4_SENDMSG:
		case BPF_CGROUP_UDP6_SENDMSG:
		case BPF_CGROUP_UNIX_SENDMSG:
		case BPF_CGROUP_UDP4_RECVMSG:
		case BPF_CGROUP_UDP6_RECVMSG:
		case BPF_CGROUP_UNIX_RECVMSG:
			return 0;
		default:
			return -EINVAL;
		}
	case BPF_PROG_TYPE_CGROUP_SKB:
		switch (expected_attach_type) {
		case BPF_CGROUP_INET_INGRESS:
		case BPF_CGROUP_INET_EGRESS:
			return 0;
		default:
			return -EINVAL;
		}
	case BPF_PROG_TYPE_CGROUP_SOCKOPT:
		switch (expected_attach_type) {
		case BPF_CGROUP_SETSOCKOPT:
		case BPF_CGROUP_GETSOCKOPT:
			return 0;
		default:
			return -EINVAL;
		}
	case BPF_PROG_TYPE_SK_LOOKUP:
		if (expected_attach_type == BPF_SK_LOOKUP)
			return 0;
		return -EINVAL;
	case BPF_PROG_TYPE_SK_REUSEPORT:
		switch (expected_attach_type) {
		case BPF_SK_REUSEPORT_SELECT:
		case BPF_SK_REUSEPORT_SELECT_OR_MIGRATE:
			return 0;
		default:
			return -EINVAL;
		}
	case BPF_PROG_TYPE_NETFILTER:
		if (expected_attach_type == BPF_NETFILTER)
			return 0;
		return -EINVAL;
	case BPF_PROG_TYPE_SYSCALL:
	case BPF_PROG_TYPE_EXT:
		if (expected_attach_type)
			return -EINVAL;
		fallthrough;
	default:
		return 0;
	}
}

static bool is_net_admin_prog_type(enum bpf_prog_type prog_type)
{
	switch (prog_type) {
	case BPF_PROG_TYPE_SCHED_CLS:
	case BPF_PROG_TYPE_SCHED_ACT:
	case BPF_PROG_TYPE_XDP:
	case BPF_PROG_TYPE_LWT_IN:
	case BPF_PROG_TYPE_LWT_OUT:
	case BPF_PROG_TYPE_LWT_XMIT:
	case BPF_PROG_TYPE_LWT_SEG6LOCAL:
	case BPF_PROG_TYPE_SK_SKB:
	case BPF_PROG_TYPE_SK_MSG:
	case BPF_PROG_TYPE_FLOW_DISSECTOR:
	case BPF_PROG_TYPE_CGROUP_DEVICE:
	case BPF_PROG_TYPE_CGROUP_SOCK:
	case BPF_PROG_TYPE_CGROUP_SOCK_ADDR:
	case BPF_PROG_TYPE_CGROUP_SOCKOPT:
	case BPF_PROG_TYPE_CGROUP_SYSCTL:
	case BPF_PROG_TYPE_SOCK_OPS:
	case BPF_PROG_TYPE_EXT: /* extends any prog */
	case BPF_PROG_TYPE_NETFILTER:
		return true;
	case BPF_PROG_TYPE_CGROUP_SKB:
		/* always unpriv */
	case BPF_PROG_TYPE_SK_REUSEPORT:
		/* equivalent to SOCKET_FILTER. need CAP_BPF only */
	default:
		return false;
	}
}

static bool is_perfmon_prog_type(enum bpf_prog_type prog_type)
{
	switch (prog_type) {
	case BPF_PROG_TYPE_KPROBE:
	case BPF_PROG_TYPE_TRACEPOINT:
	case BPF_PROG_TYPE_PERF_EVENT:
	case BPF_PROG_TYPE_RAW_TRACEPOINT:
	case BPF_PROG_TYPE_RAW_TRACEPOINT_WRITABLE:
	case BPF_PROG_TYPE_TRACING:
	case BPF_PROG_TYPE_LSM:
	case BPF_PROG_TYPE_STRUCT_OPS: /* has access to struct sock */
	case BPF_PROG_TYPE_EXT: /* extends any prog */
		return true;
	default:
		return false;
	}
}

/* last field in 'union bpf_attr' used by this command */
#define BPF_PROG_LOAD_LAST_FIELD fd_array_cnt

static int bpf_prog_load(union bpf_attr *attr, bpfptr_t uattr, u32 uattr_size)
{
	enum bpf_prog_type type = attr->prog_type;
	struct bpf_prog *prog, *dst_prog = NULL;
	struct btf *attach_btf = NULL;
	struct bpf_token *token = NULL;
	bool bpf_cap;
	int err;
	char license[128];

	if (CHECK_ATTR(BPF_PROG_LOAD))
		return -EINVAL;

	if (attr->prog_flags & ~(BPF_F_STRICT_ALIGNMENT |
				 BPF_F_ANY_ALIGNMENT |
				 BPF_F_TEST_STATE_FREQ |
				 BPF_F_SLEEPABLE |
				 BPF_F_TEST_RND_HI32 |
				 BPF_F_XDP_HAS_FRAGS |
				 BPF_F_XDP_DEV_BOUND_ONLY |
				 BPF_F_TEST_REG_INVARIANTS |
				 BPF_F_TOKEN_FD))
		return -EINVAL;

	bpf_prog_load_fixup_attach_type(attr);

	if (attr->prog_flags & BPF_F_TOKEN_FD) {
		token = bpf_token_get_from_fd(attr->prog_token_fd);
		if (IS_ERR(token))
			return PTR_ERR(token);
		/* if current token doesn't grant prog loading permissions,
		 * then we can't use this token, so ignore it and rely on
		 * system-wide capabilities checks
		 */
		if (!bpf_token_allow_cmd(token, BPF_PROG_LOAD) ||
		    !bpf_token_allow_prog_type(token, attr->prog_type,
					       attr->expected_attach_type)) {
			bpf_token_put(token);
			token = NULL;
		}
	}

	bpf_cap = bpf_token_capable(token, CAP_BPF);
	err = -EPERM;

	if (!IS_ENABLED(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) &&
	    (attr->prog_flags & BPF_F_ANY_ALIGNMENT) &&
	    !bpf_cap)
		goto put_token;

	/* Intent here is for unprivileged_bpf_disabled to block BPF program
	 * creation for unprivileged users; other actions depend
	 * on fd availability and access to bpffs, so are dependent on
	 * object creation success. Even with unprivileged BPF disabled,
	 * capability checks are still carried out for these
	 * and other operations.
	 */
	if (sysctl_unprivileged_bpf_disabled && !bpf_cap)
		goto put_token;

	if (attr->insn_cnt == 0 ||
	    attr->insn_cnt > (bpf_cap ? BPF_COMPLEXITY_LIMIT_INSNS : BPF_MAXINSNS)) {
		err = -E2BIG;
		goto put_token;
	}
	if (type != BPF_PROG_TYPE_SOCKET_FILTER &&
	    type != BPF_PROG_TYPE_CGROUP_SKB &&
	    !bpf_cap)
		goto put_token;

	if (is_net_admin_prog_type(type) && !bpf_token_capable(token, CAP_NET_ADMIN))
		goto put_token;
	if (is_perfmon_prog_type(type) && !bpf_token_capable(token, CAP_PERFMON))
		goto put_token;

	/* attach_prog_fd/attach_btf_obj_fd can specify fd of either bpf_prog
	 * or btf, we need to check which one it is
	 */
	if (attr->attach_prog_fd) {
		dst_prog = bpf_prog_get(attr->attach_prog_fd);
		if (IS_ERR(dst_prog)) {
			dst_prog = NULL;
			attach_btf = btf_get_by_fd(attr->attach_btf_obj_fd);
			if (IS_ERR(attach_btf)) {
				err = -EINVAL;
				goto put_token;
			}
			if (!btf_is_kernel(attach_btf)) {
				/* attaching through specifying bpf_prog's BTF
				 * objects directly might be supported eventually
				 */
				btf_put(attach_btf);
				err = -ENOTSUPP;
				goto put_token;
			}
		}
	} else if (attr->attach_btf_id) {
		/* fall back to vmlinux BTF, if BTF type ID is specified */
		attach_btf = bpf_get_btf_vmlinux();
		if (IS_ERR(attach_btf)) {
			err = PTR_ERR(attach_btf);
			goto put_token;
		}
		if (!attach_btf) {
			err = -EINVAL;
			goto put_token;
		}
		btf_get(attach_btf);
	}

	if (bpf_prog_load_check_attach(type, attr->expected_attach_type,
				       attach_btf, attr->attach_btf_id,
				       dst_prog)) {
		if (dst_prog)
			bpf_prog_put(dst_prog);
		if (attach_btf)
			btf_put(attach_btf);
		err = -EINVAL;
		goto put_token;
	}

	/* plain bpf_prog allocation */
	prog = bpf_prog_alloc(bpf_prog_size(attr->insn_cnt), GFP_USER);
	if (!prog) {
		if (dst_prog)
			bpf_prog_put(dst_prog);
		if (attach_btf)
			btf_put(attach_btf);
		err = -EINVAL;
		goto put_token;
	}

	prog->expected_attach_type = attr->expected_attach_type;
	prog->sleepable = !!(attr->prog_flags & BPF_F_SLEEPABLE);
	prog->aux->attach_btf = attach_btf;
	prog->aux->attach_btf_id = attr->attach_btf_id;
	prog->aux->dst_prog = dst_prog;
	prog->aux->dev_bound = !!attr->prog_ifindex;
	prog->aux->xdp_has_frags = attr->prog_flags & BPF_F_XDP_HAS_FRAGS;

	/* move token into prog->aux, reuse taken refcnt */
	prog->aux->token = token;
	token = NULL;

	prog->aux->user = get_current_user();
	prog->len = attr->insn_cnt;

	err = -EFAULT;
	if (copy_from_bpfptr(prog->insns,
			     make_bpfptr(attr->insns, uattr.is_kernel),
			     bpf_prog_insn_size(prog)) != 0)
		goto free_prog;
	/* copy eBPF program license from user space */
	if (strncpy_from_bpfptr(license,
				make_bpfptr(attr->license, uattr.is_kernel),
				sizeof(license) - 1) < 0)
		goto free_prog;
	license[sizeof(license) - 1] = 0;

	/* eBPF programs must be GPL compatible to use GPL-ed functions */
	prog->gpl_compatible = license_is_gpl_compatible(license) ? 1 : 0;

	prog->orig_prog = NULL;
	prog->jited = 0;

	atomic64_set(&prog->aux->refcnt, 1);

	if (bpf_prog_is_dev_bound(prog->aux)) {
		err = bpf_prog_dev_bound_init(prog, attr);
		if (err)
			goto free_prog;
	}

	if (type == BPF_PROG_TYPE_EXT && dst_prog &&
	    bpf_prog_is_dev_bound(dst_prog->aux)) {
		err = bpf_prog_dev_bound_inherit(prog, dst_prog);
		if (err)
			goto free_prog;
	}

	/*
	 * Bookkeeping for managing the program attachment chain.
	 *
	 * It might be tempting to set attach_tracing_prog flag at the attachment
	 * time, but this will not prevent from loading bunch of tracing prog
	 * first, then attach them one to another.
	 *
	 * The flag attach_tracing_prog is set for the whole program lifecycle, and
	 * doesn't have to be cleared in bpf_tracing_link_release, since tracing
	 * programs cannot change attachment target.
	 */
	if (type == BPF_PROG_TYPE_TRACING && dst_prog &&
	    dst_prog->type == BPF_PROG_TYPE_TRACING) {
		prog->aux->attach_tracing_prog = true;
	}

	/* find program type: socket_filter vs tracing_filter */
	err = find_prog_type(type, prog);
	if (err < 0)
		goto free_prog;

	prog->aux->load_time = ktime_get_boottime_ns();
	err = bpf_obj_name_cpy(prog->aux->name, attr->prog_name,
			       sizeof(attr->prog_name));
	if (err < 0)
		goto free_prog;

	err = security_bpf_prog_load(prog, attr, token, uattr.is_kernel);
	if (err)
		goto free_prog_sec;

	/* run eBPF verifier */
	err = bpf_check(&prog, attr, uattr, uattr_size);
	if (err < 0)
		goto free_used_maps;

	prog = bpf_prog_select_runtime(prog, &err);
	if (err < 0)
		goto free_used_maps;

	err = bpf_prog_alloc_id(prog);
	if (err)
		goto free_used_maps;

	/* Upon success of bpf_prog_alloc_id(), the BPF prog is
	 * effectively publicly exposed. However, retrieving via
	 * bpf_prog_get_fd_by_id() will take another reference,
	 * therefore it cannot be gone underneath us.
	 *
	 * Only for the time /after/ successful bpf_prog_new_fd()
	 * and before returning to userspace, we might just hold
	 * one reference and any parallel close on that fd could
	 * rip everything out. Hence, below notifications must
	 * happen before bpf_prog_new_fd().
	 *
	 * Also, any failure handling from this point onwards must
	 * be using bpf_prog_put() given the program is exposed.
	 */
	bpf_prog_kallsyms_add(prog);
	perf_event_bpf_event(prog, PERF_BPF_EVENT_PROG_LOAD, 0);
	bpf_audit_prog(prog, BPF_AUDIT_LOAD);

	err = bpf_prog_new_fd(prog);
	if (err < 0)
		bpf_prog_put(prog);
	return err;

free_used_maps:
	/* In case we have subprogs, we need to wait for a grace
	 * period before we can tear down JIT memory since symbols
	 * are already exposed under kallsyms.
	 */
	__bpf_prog_put_noref(prog, prog->aux->real_func_cnt);
	return err;

free_prog_sec:
	security_bpf_prog_free(prog);
free_prog:
	free_uid(prog->aux->user);
	if (prog->aux->attach_btf)
		btf_put(prog->aux->attach_btf);
	bpf_prog_free(prog);
put_token:
	bpf_token_put(token);
	return err;
}

#define BPF_OBJ_LAST_FIELD path_fd

static int bpf_obj_pin(const union bpf_attr *attr)
{
	int path_fd;

	if (CHECK_ATTR(BPF_OBJ) || attr->file_flags & ~BPF_F_PATH_FD)
		return -EINVAL;

	/* path_fd has to be accompanied by BPF_F_PATH_FD flag */
	if (!(attr->file_flags & BPF_F_PATH_FD) && attr->path_fd)
		return -EINVAL;

	path_fd = attr->file_flags & BPF_F_PATH_FD ? attr->path_fd : AT_FDCWD;
	return bpf_obj_pin_user(attr->bpf_fd, path_fd,
				u64_to_user_ptr(attr->pathname));
}

static int bpf_obj_get(const union bpf_attr *attr)
{
	int path_fd;

	if (CHECK_ATTR(BPF_OBJ) || attr->bpf_fd != 0 ||
	    attr->file_flags & ~(BPF_OBJ_FLAG_MASK | BPF_F_PATH_FD))
		return -EINVAL;

	/* path_fd has to be accompanied by BPF_F_PATH_FD flag */
	if (!(attr->file_flags & BPF_F_PATH_FD) && attr->path_fd)
		return -EINVAL;

	path_fd = attr->file_flags & BPF_F_PATH_FD ? attr->path_fd : AT_FDCWD;
	return bpf_obj_get_user(path_fd, u64_to_user_ptr(attr->pathname),
				attr->file_flags);
}

/* bpf_link_init_sleepable() allows to specify whether BPF link itself has
 * "sleepable" semantics, which normally would mean that BPF link's attach
 * hook can dereference link or link's underlying program for some time after
 * detachment due to RCU Tasks Trace-based lifetime protection scheme.
 * BPF program itself can be non-sleepable, yet, because it's transitively
 * reachable through BPF link, its freeing has to be delayed until after RCU
 * Tasks Trace GP.
 */
void bpf_link_init_sleepable(struct bpf_link *link, enum bpf_link_type type,
			     const struct bpf_link_ops *ops, struct bpf_prog *prog,
			     bool sleepable)
{
	WARN_ON(ops->dealloc && ops->dealloc_deferred);
	atomic64_set(&link->refcnt, 1);
	link->type = type;
	link->sleepable = sleepable;
	link->id = 0;
	link->ops = ops;
	link->prog = prog;
}

void bpf_link_init(struct bpf_link *link, enum bpf_link_type type,
		   const struct bpf_link_ops *ops, struct bpf_prog *prog)
{
	bpf_link_init_sleepable(link, type, ops, prog, false);
}

static void bpf_link_free_id(int id)
{
	if (!id)
		return;

	spin_lock_bh(&link_idr_lock);
	idr_remove(&link_idr, id);
	spin_unlock_bh(&link_idr_lock);
}

/* Clean up bpf_link and corresponding anon_inode file and FD. After
 * anon_inode is created, bpf_link can't be just kfree()'d due to deferred
 * anon_inode's release() call. This helper marks bpf_link as
 * defunct, releases anon_inode file and puts reserved FD. bpf_prog's refcnt
 * is not decremented, it's the responsibility of a calling code that failed
 * to complete bpf_link initialization.
 * This helper eventually calls link's dealloc callback, but does not call
 * link's release callback.
 */
void bpf_link_cleanup(struct bpf_link_primer *primer)
{
	primer->link->prog = NULL;
	bpf_link_free_id(primer->id);
	fput(primer->file);
	put_unused_fd(primer->fd);
}

void bpf_link_inc(struct bpf_link *link)
{
	atomic64_inc(&link->refcnt);
}

static void bpf_link_dealloc(struct bpf_link *link)
{
	/* now that we know that bpf_link itself can't be reached, put underlying BPF program */
	if (link->prog)
		bpf_prog_put(link->prog);

	/* free bpf_link and its containing memory */
	if (link->ops->dealloc_deferred)
		link->ops->dealloc_deferred(link);
	else
		link->ops->dealloc(link);
}

static void bpf_link_defer_dealloc_rcu_gp(struct rcu_head *rcu)
{
	struct bpf_link *link = container_of(rcu, struct bpf_link, rcu);

	bpf_link_dealloc(link);
}

static void bpf_link_defer_dealloc_mult_rcu_gp(struct rcu_head *rcu)
{
	if (rcu_trace_implies_rcu_gp())
		bpf_link_defer_dealloc_rcu_gp(rcu);
	else
		call_rcu(rcu, bpf_link_defer_dealloc_rcu_gp);
}

/* bpf_link_free is guaranteed to be called from process context */
static void bpf_link_free(struct bpf_link *link)
{
	const struct bpf_link_ops *ops = link->ops;

	bpf_link_free_id(link->id);
	/* detach BPF program, clean up used resources */
	if (link->prog)
		ops->release(link);
	if (ops->dealloc_deferred) {
		/* Schedule BPF link deallocation, which will only then
		 * trigger putting BPF program refcount.
		 * If underlying BPF program is sleepable or BPF link's target
		 * attach hookpoint is sleepable or otherwise requires RCU GPs
		 * to ensure link and its underlying BPF program is not
		 * reachable anymore, we need to first wait for RCU tasks
		 * trace sync, and then go through "classic" RCU grace period
		 */
		if (link->sleepable || (link->prog && link->prog->sleepable))
			call_rcu_tasks_trace(&link->rcu, bpf_link_defer_dealloc_mult_rcu_gp);
		else
			call_rcu(&link->rcu, bpf_link_defer_dealloc_rcu_gp);
	} else if (ops->dealloc) {
		bpf_link_dealloc(link);
	}
}

static void bpf_link_put_deferred(struct work_struct *work)
{
	struct bpf_link *link = container_of(work, struct bpf_link, work);

	bpf_link_free(link);
}

/* bpf_link_put might be called from atomic context. It needs to be called
 * from sleepable context in order to acquire sleeping locks during the process.
 */
void bpf_link_put(struct bpf_link *link)
{
	if (!atomic64_dec_and_test(&link->refcnt))
		return;

	INIT_WORK(&link->work, bpf_link_put_deferred);
	schedule_work(&link->work);
}
EXPORT_SYMBOL(bpf_link_put);

static void bpf_link_put_direct(struct bpf_link *link)
{
	if (!atomic64_dec_and_test(&link->refcnt))
		return;
	bpf_link_free(link);
}

static int bpf_link_release(struct inode *inode, struct file *filp)
{
	struct bpf_link *link = filp->private_data;

	bpf_link_put_direct(link);
	return 0;
}

#ifdef CONFIG_PROC_FS
#define BPF_PROG_TYPE(_id, _name, prog_ctx_type, kern_ctx_type)
#define BPF_MAP_TYPE(_id, _ops)
#define BPF_LINK_TYPE(_id, _name) [_id] = #_name,
static const char *bpf_link_type_strs[] = {
	[BPF_LINK_TYPE_UNSPEC] = "<invalid>",
#include <linux/bpf_types.h>
};
#undef BPF_PROG_TYPE
#undef BPF_MAP_TYPE
#undef BPF_LINK_TYPE

static void bpf_link_show_fdinfo(struct seq_file *m, struct file *filp)
{
	const struct bpf_link *link = filp->private_data;
	const struct bpf_prog *prog = link->prog;
	enum bpf_link_type type = link->type;
	char prog_tag[sizeof(prog->tag) * 2 + 1] = { };

	if (type < ARRAY_SIZE(bpf_link_type_strs) && bpf_link_type_strs[type]) {
		seq_printf(m, "link_type:\t%s\n", bpf_link_type_strs[type]);
	} else {
		WARN_ONCE(1, "missing BPF_LINK_TYPE(...) for link type %u\n", type);
		seq_printf(m, "link_type:\t<%u>\n", type);
	}
	seq_printf(m, "link_id:\t%u\n", link->id);

	if (prog) {
		bin2hex(prog_tag, prog->tag, sizeof(prog->tag));
		seq_printf(m,
			   "prog_tag:\t%s\n"
			   "prog_id:\t%u\n",
			   prog_tag,
			   prog->aux->id);
	}
	if (link->ops->show_fdinfo)
		link->ops->show_fdinfo(link, m);
}
#endif

static __poll_t bpf_link_poll(struct file *file, struct poll_table_struct *pts)
{
	struct bpf_link *link = file->private_data;

	return link->ops->poll(file, pts);
}

static const struct file_operations bpf_link_fops = {
#ifdef CONFIG_PROC_FS
	.show_fdinfo	= bpf_link_show_fdinfo,
#endif
	.release	= bpf_link_release,
	.read		= bpf_dummy_read,
	.write		= bpf_dummy_write,
};

static const struct file_operations bpf_link_fops_poll = {
#ifdef CONFIG_PROC_FS
	.show_fdinfo	= bpf_link_show_fdinfo,
#endif
	.release	= bpf_link_release,
	.read		= bpf_dummy_read,
	.write		= bpf_dummy_write,
	.poll		= bpf_link_poll,
};

static int bpf_link_alloc_id(struct bpf_link *link)
{
	int id;

	idr_preload(GFP_KERNEL);
	spin_lock_bh(&link_idr_lock);
	id = idr_alloc_cyclic(&link_idr, link, 1, INT_MAX, GFP_ATOMIC);
	spin_unlock_bh(&link_idr_lock);
	idr_preload_end();

	return id;
}

/* Prepare bpf_link to be exposed to user-space by allocating anon_inode file,
 * reserving unused FD and allocating ID from link_idr. This is to be paired
 * with bpf_link_settle() to install FD and ID and expose bpf_link to
 * user-space, if bpf_link is successfully attached. If not, bpf_link and
 * pre-allocated resources are to be freed with bpf_cleanup() call. All the
 * transient state is passed around in struct bpf_link_primer.
 * This is preferred way to create and initialize bpf_link, especially when
 * there are complicated and expensive operations in between creating bpf_link
 * itself and attaching it to BPF hook. By using bpf_link_prime() and
 * bpf_link_settle() kernel code using bpf_link doesn't have to perform
 * expensive (and potentially failing) roll back operations in a rare case
 * that file, FD, or ID can't be allocated.
 */
int bpf_link_prime(struct bpf_link *link, struct bpf_link_primer *primer)
{
	struct file *file;
	int fd, id;

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0)
		return fd;


	id = bpf_link_alloc_id(link);
	if (id < 0) {
		put_unused_fd(fd);
		return id;
	}

	file = anon_inode_getfile("bpf_link",
				  link->ops->poll ? &bpf_link_fops_poll : &bpf_link_fops,
				  link, O_CLOEXEC);
	if (IS_ERR(file)) {
		bpf_link_free_id(id);
		put_unused_fd(fd);
		return PTR_ERR(file);
	}

	primer->link = link;
	primer->file = file;
	primer->fd = fd;
	primer->id = id;
	return 0;
}

int bpf_link_settle(struct bpf_link_primer *primer)
{
	/* make bpf_link fetchable by ID */
	spin_lock_bh(&link_idr_lock);
	primer->link->id = primer->id;
	spin_unlock_bh(&link_idr_lock);
	/* make bpf_link fetchable by FD */
	fd_install(primer->fd, primer->file);
	/* pass through installed FD */
	return primer->fd;
}

int bpf_link_new_fd(struct bpf_link *link)
{
	return anon_inode_getfd("bpf-link",
				link->ops->poll ? &bpf_link_fops_poll : &bpf_link_fops,
				link, O_CLOEXEC);
}

struct bpf_link *bpf_link_get_from_fd(u32 ufd)
{
	CLASS(fd, f)(ufd);
	struct bpf_link *link;

	if (fd_empty(f))
		return ERR_PTR(-EBADF);
	if (fd_file(f)->f_op != &bpf_link_fops && fd_file(f)->f_op != &bpf_link_fops_poll)
		return ERR_PTR(-EINVAL);

	link = fd_file(f)->private_data;
	bpf_link_inc(link);
	return link;
}
EXPORT_SYMBOL_NS(bpf_link_get_from_fd, "BPF_INTERNAL");

static void bpf_tracing_link_release(struct bpf_link *link)
{
	struct bpf_tracing_link *tr_link =
		container_of(link, struct bpf_tracing_link, link.link);

	WARN_ON_ONCE(bpf_trampoline_unlink_prog(&tr_link->link,
						tr_link->trampoline,
						tr_link->tgt_prog));

	bpf_trampoline_put(tr_link->trampoline);

	/* tgt_prog is NULL if target is a kernel function */
	if (tr_link->tgt_prog)
		bpf_prog_put(tr_link->tgt_prog);
}

static void bpf_tracing_link_dealloc(struct bpf_link *link)
{
	struct bpf_tracing_link *tr_link =
		container_of(link, struct bpf_tracing_link, link.link);

	kfree(tr_link);
}

static void bpf_tracing_link_show_fdinfo(const struct bpf_link *link,
					 struct seq_file *seq)
{
	struct bpf_tracing_link *tr_link =
		container_of(link, struct bpf_tracing_link, link.link);
	u32 target_btf_id, target_obj_id;

	bpf_trampoline_unpack_key(tr_link->trampoline->key,
				  &target_obj_id, &target_btf_id);
	seq_printf(seq,
		   "attach_type:\t%d\n"
		   "target_obj_id:\t%u\n"
		   "target_btf_id:\t%u\n",
		   tr_link->attach_type,
		   target_obj_id,
		   target_btf_id);
}

static int bpf_tracing_link_fill_link_info(const struct bpf_link *link,
					   struct bpf_link_info *info)
{
	struct bpf_tracing_link *tr_link =
		container_of(link, struct bpf_tracing_link, link.link);

	info->tracing.attach_type = tr_link->attach_type;
	bpf_trampoline_unpack_key(tr_link->trampoline->key,
				  &info->tracing.target_obj_id,
				  &info->tracing.target_btf_id);

	return 0;
}

static const struct bpf_link_ops bpf_tracing_link_lops = {
	.release = bpf_tracing_link_release,
	.dealloc = bpf_tracing_link_dealloc,
	.show_fdinfo = bpf_tracing_link_show_fdinfo,
	.fill_link_info = bpf_tracing_link_fill_link_info,
};

static int bpf_tracing_prog_attach(struct bpf_prog *prog,
				   int tgt_prog_fd,
				   u32 btf_id,
				   u64 bpf_cookie)
{
	struct bpf_link_primer link_primer;
	struct bpf_prog *tgt_prog = NULL;
	struct bpf_trampoline *tr = NULL;
	struct bpf_tracing_link *link;
	u64 key = 0;
	int err;

	switch (prog->type) {
	case BPF_PROG_TYPE_TRACING:
		if (prog->expected_attach_type != BPF_TRACE_FENTRY &&
		    prog->expected_attach_type != BPF_TRACE_FEXIT &&
		    prog->expected_attach_type != BPF_MODIFY_RETURN) {
			err = -EINVAL;
			goto out_put_prog;
		}
		break;
	case BPF_PROG_TYPE_EXT:
		if (prog->expected_attach_type != 0) {
			err = -EINVAL;
			goto out_put_prog;
		}
		break;
	case BPF_PROG_TYPE_LSM:
		if (prog->expected_attach_type != BPF_LSM_MAC) {
			err = -EINVAL;
			goto out_put_prog;
		}
		break;
	default:
		err = -EINVAL;
		goto out_put_prog;
	}

	if (!!tgt_prog_fd != !!btf_id) {
		err = -EINVAL;
		goto out_put_prog;
	}

	if (tgt_prog_fd) {
		/*
		 * For now we only allow new targets for BPF_PROG_TYPE_EXT. If this
		 * part would be changed to implement the same for
		 * BPF_PROG_TYPE_TRACING, do not forget to update the way how
		 * attach_tracing_prog flag is set.
		 */
		if (prog->type != BPF_PROG_TYPE_EXT) {
			err = -EINVAL;
			goto out_put_prog;
		}

		tgt_prog = bpf_prog_get(tgt_prog_fd);
		if (IS_ERR(tgt_prog)) {
			err = PTR_ERR(tgt_prog);
			tgt_prog = NULL;
			goto out_put_prog;
		}

		key = bpf_trampoline_compute_key(tgt_prog, NULL, btf_id);
	}

	link = kzalloc(sizeof(*link), GFP_USER);
	if (!link) {
		err = -ENOMEM;
		goto out_put_prog;
	}
	bpf_link_init(&link->link.link, BPF_LINK_TYPE_TRACING,
		      &bpf_tracing_link_lops, prog);
	link->attach_type = prog->expected_attach_type;
	link->link.cookie = bpf_cookie;

	mutex_lock(&prog->aux->dst_mutex);

	/* There are a few possible cases here:
	 *
	 * - if prog->aux->dst_trampoline is set, the program was just loaded
	 *   and not yet attached to anything, so we can use the values stored
	 *   in prog->aux
	 *
	 * - if prog->aux->dst_trampoline is NULL, the program has already been
	 *   attached to a target and its initial target was cleared (below)
	 *
	 * - if tgt_prog != NULL, the caller specified tgt_prog_fd +
	 *   target_btf_id using the link_create API.
	 *
	 * - if tgt_prog == NULL when this function was called using the old
	 *   raw_tracepoint_open API, and we need a target from prog->aux
	 *
	 * - if prog->aux->dst_trampoline and tgt_prog is NULL, the program
	 *   was detached and is going for re-attachment.
	 *
	 * - if prog->aux->dst_trampoline is NULL and tgt_prog and prog->aux->attach_btf
	 *   are NULL, then program was already attached and user did not provide
	 *   tgt_prog_fd so we have no way to find out or create trampoline
	 */
	if (!prog->aux->dst_trampoline && !tgt_prog) {
		/*
		 * Allow re-attach for TRACING and LSM programs. If it's
		 * currently linked, bpf_trampoline_link_prog will fail.
		 * EXT programs need to specify tgt_prog_fd, so they
		 * re-attach in separate code path.
		 */
		if (prog->type != BPF_PROG_TYPE_TRACING &&
		    prog->type != BPF_PROG_TYPE_LSM) {
			err = -EINVAL;
			goto out_unlock;
		}
		/* We can allow re-attach only if we have valid attach_btf. */
		if (!prog->aux->attach_btf) {
			err = -EINVAL;
			goto out_unlock;
		}
		btf_id = prog->aux->attach_btf_id;
		key = bpf_trampoline_compute_key(NULL, prog->aux->attach_btf, btf_id);
	}

	if (!prog->aux->dst_trampoline ||
	    (key && key != prog->aux->dst_trampoline->key)) {
		/* If there is no saved target, or the specified target is
		 * different from the destination specified at load time, we
		 * need a new trampoline and a check for compatibility
		 */
		struct bpf_attach_target_info tgt_info = {};

		err = bpf_check_attach_target(NULL, prog, tgt_prog, btf_id,
					      &tgt_info);
		if (err)
			goto out_unlock;

		if (tgt_info.tgt_mod) {
			module_put(prog->aux->mod);
			prog->aux->mod = tgt_info.tgt_mod;
		}

		tr = bpf_trampoline_get(key, &tgt_info);
		if (!tr) {
			err = -ENOMEM;
			goto out_unlock;
		}
	} else {
		/* The caller didn't specify a target, or the target was the
		 * same as the destination supplied during program load. This
		 * means we can reuse the trampoline and reference from program
		 * load time, and there is no need to allocate a new one. This
		 * can only happen once for any program, as the saved values in
		 * prog->aux are cleared below.
		 */
		tr = prog->aux->dst_trampoline;
		tgt_prog = prog->aux->dst_prog;
	}

	err = bpf_link_prime(&link->link.link, &link_primer);
	if (err)
		goto out_unlock;

	err = bpf_trampoline_link_prog(&link->link, tr, tgt_prog);
	if (err) {
		bpf_link_cleanup(&link_primer);
		link = NULL;
		goto out_unlock;
	}

	link->tgt_prog = tgt_prog;
	link->trampoline = tr;

	/* Always clear the trampoline and target prog from prog->aux to make
	 * sure the original attach destination is not kept alive after a
	 * program is (re-)attached to another target.
	 */
	if (prog->aux->dst_prog &&
	    (tgt_prog_fd || tr != prog->aux->dst_trampoline))
		/* got extra prog ref from syscall, or attaching to different prog */
		bpf_prog_put(prog->aux->dst_prog);
	if (prog->aux->dst_trampoline && tr != prog->aux->dst_trampoline)
		/* we allocated a new trampoline, so free the old one */
		bpf_trampoline_put(prog->aux->dst_trampoline);

	prog->aux->dst_prog = NULL;
	prog->aux->dst_trampoline = NULL;
	mutex_unlock(&prog->aux->dst_mutex);

	return bpf_link_settle(&link_primer);
out_unlock:
	if (tr && tr != prog->aux->dst_trampoline)
		bpf_trampoline_put(tr);
	mutex_unlock(&prog->aux->dst_mutex);
	kfree(link);
out_put_prog:
	if (tgt_prog_fd && tgt_prog)
		bpf_prog_put(tgt_prog);
	return err;
}

static void bpf_raw_tp_link_release(struct bpf_link *link)
{
	struct bpf_raw_tp_link *raw_tp =
		container_of(link, struct bpf_raw_tp_link, link);

	bpf_probe_unregister(raw_tp->btp, raw_tp);
	bpf_put_raw_tracepoint(raw_tp->btp);
}

static void bpf_raw_tp_link_dealloc(struct bpf_link *link)
{
	struct bpf_raw_tp_link *raw_tp =
		container_of(link, struct bpf_raw_tp_link, link);

	kfree(raw_tp);
}

static void bpf_raw_tp_link_show_fdinfo(const struct bpf_link *link,
					struct seq_file *seq)
{
	struct bpf_raw_tp_link *raw_tp_link =
		container_of(link, struct bpf_raw_tp_link, link);

	seq_printf(seq,
		   "tp_name:\t%s\n",
		   raw_tp_link->btp->tp->name);
}

static int bpf_copy_to_user(char __user *ubuf, const char *buf, u32 ulen,
			    u32 len)
{
	if (ulen >= len + 1) {
		if (copy_to_user(ubuf, buf, len + 1))
			return -EFAULT;
	} else {
		char zero = '\0';

		if (copy_to_user(ubuf, buf, ulen - 1))
			return -EFAULT;
		if (put_user(zero, ubuf + ulen - 1))
			return -EFAULT;
		return -ENOSPC;
	}

	return 0;
}

static int bpf_raw_tp_link_fill_link_info(const struct bpf_link *link,
					  struct bpf_link_info *info)
{
	struct bpf_raw_tp_link *raw_tp_link =
		container_of(link, struct bpf_raw_tp_link, link);
	char __user *ubuf = u64_to_user_ptr(info->raw_tracepoint.tp_name);
	const char *tp_name = raw_tp_link->btp->tp->name;
	u32 ulen = info->raw_tracepoint.tp_name_len;
	size_t tp_len = strlen(tp_name);

	if (!ulen ^ !ubuf)
		return -EINVAL;

	info->raw_tracepoint.tp_name_len = tp_len + 1;

	if (!ubuf)
		return 0;

	return bpf_copy_to_user(ubuf, tp_name, ulen, tp_len);
}

static const struct bpf_link_ops bpf_raw_tp_link_lops = {
	.release = bpf_raw_tp_link_release,
	.dealloc_deferred = bpf_raw_tp_link_dealloc,
	.show_fdinfo = bpf_raw_tp_link_show_fdinfo,
	.fill_link_info = bpf_raw_tp_link_fill_link_info,
};

#ifdef CONFIG_PERF_EVENTS
struct bpf_perf_link {
	struct bpf_link link;
	struct file *perf_file;
};

static void bpf_perf_link_release(struct bpf_link *link)
{
	struct bpf_perf_link *perf_link = container_of(link, struct bpf_perf_link, link);
	struct perf_event *event = perf_link->perf_file->private_data;

	perf_event_free_bpf_prog(event);
	fput(perf_link->perf_file);
}

static void bpf_perf_link_dealloc(struct bpf_link *link)
{
	struct bpf_perf_link *perf_link = container_of(link, struct bpf_perf_link, link);

	kfree(perf_link);
}

static int bpf_perf_link_fill_common(const struct perf_event *event,
				     char __user *uname, u32 *ulenp,
				     u64 *probe_offset, u64 *probe_addr,
				     u32 *fd_type, unsigned long *missed)
{
	const char *buf;
	u32 prog_id, ulen;
	size_t len;
	int err;

	ulen = *ulenp;
	if (!ulen ^ !uname)
		return -EINVAL;

	err = bpf_get_perf_event_info(event, &prog_id, fd_type, &buf,
				      probe_offset, probe_addr, missed);
	if (err)
		return err;

	if (buf) {
		len = strlen(buf);
		*ulenp = len + 1;
	} else {
		*ulenp = 1;
	}
	if (!uname)
		return 0;

	if (buf) {
		err = bpf_copy_to_user(uname, buf, ulen, len);
		if (err)
			return err;
	} else {
		char zero = '\0';

		if (put_user(zero, uname))
			return -EFAULT;
	}
	return 0;
}

#ifdef CONFIG_KPROBE_EVENTS
static int bpf_perf_link_fill_kprobe(const struct perf_event *event,
				     struct bpf_link_info *info)
{
	unsigned long missed;
	char __user *uname;
	u64 addr, offset;
	u32 ulen, type;
	int err;

	uname = u64_to_user_ptr(info->perf_event.kprobe.func_name);
	ulen = info->perf_event.kprobe.name_len;
	err = bpf_perf_link_fill_common(event, uname, &ulen, &offset, &addr,
					&type, &missed);
	if (err)
		return err;
	if (type == BPF_FD_TYPE_KRETPROBE)
		info->perf_event.type = BPF_PERF_EVENT_KRETPROBE;
	else
		info->perf_event.type = BPF_PERF_EVENT_KPROBE;
	info->perf_event.kprobe.name_len = ulen;
	info->perf_event.kprobe.offset = offset;
	info->perf_event.kprobe.missed = missed;
	if (!kallsyms_show_value(current_cred()))
		addr = 0;
	info->perf_event.kprobe.addr = addr;
	info->perf_event.kprobe.cookie = event->bpf_cookie;
	return 0;
}
#endif

#ifdef CONFIG_UPROBE_EVENTS
static int bpf_perf_link_fill_uprobe(const struct perf_event *event,
				     struct bpf_link_info *info)
{
	u64 ref_ctr_offset, offset;
	char __user *uname;
	u32 ulen, type;
	int err;

	uname = u64_to_user_ptr(info->perf_event.uprobe.file_name);
	ulen = info->perf_event.uprobe.name_len;
	err = bpf_perf_link_fill_common(event, uname, &ulen, &offset, &ref_ctr_offset,
					&type, NULL);
	if (err)
		return err;

	if (type == BPF_FD_TYPE_URETPROBE)
		info->perf_event.type = BPF_PERF_EVENT_URETPROBE;
	else
		info->perf_event.type = BPF_PERF_EVENT_UPROBE;
	info->perf_event.uprobe.name_len = ulen;
	info->perf_event.uprobe.offset = offset;
	info->perf_event.uprobe.cookie = event->bpf_cookie;
	info->perf_event.uprobe.ref_ctr_offset = ref_ctr_offset;
	return 0;
}
#endif

static int bpf_perf_link_fill_probe(const struct perf_event *event,
				    struct bpf_link_info *info)
{
#ifdef CONFIG_KPROBE_EVENTS
	if (event->tp_event->flags & TRACE_EVENT_FL_KPROBE)
		return bpf_perf_link_fill_kprobe(event, info);
#endif
#ifdef CONFIG_UPROBE_EVENTS
	if (event->tp_event->flags & TRACE_EVENT_FL_UPROBE)
		return bpf_perf_link_fill_uprobe(event, info);
#endif
	return -EOPNOTSUPP;
}

static int bpf_perf_link_fill_tracepoint(const struct perf_event *event,
					 struct bpf_link_info *info)
{
	char __user *uname;
	u32 ulen;
	int err;

	uname = u64_to_user_ptr(info->perf_event.tracepoint.tp_name);
	ulen = info->perf_event.tracepoint.name_len;
	err = bpf_perf_link_fill_common(event, uname, &ulen, NULL, NULL, NULL, NULL);
	if (err)
		return err;

	info->perf_event.type = BPF_PERF_EVENT_TRACEPOINT;
	info->perf_event.tracepoint.name_len = ulen;
	info->perf_event.tracepoint.cookie = event->bpf_cookie;
	return 0;
}

static int bpf_perf_link_fill_perf_event(const struct perf_event *event,
					 struct bpf_link_info *info)
{
	info->perf_event.event.type = event->attr.type;
	info->perf_event.event.config = event->attr.config;
	info->perf_event.event.cookie = event->bpf_cookie;
	info->perf_event.type = BPF_PERF_EVENT_EVENT;
	return 0;
}

static int bpf_perf_link_fill_link_info(const struct bpf_link *link,
					struct bpf_link_info *info)
{
	struct bpf_perf_link *perf_link;
	const struct perf_event *event;

	perf_link = container_of(link, struct bpf_perf_link, link);
	event = perf_get_event(perf_link->perf_file);
	if (IS_ERR(event))
		return PTR_ERR(event);

	switch (event->prog->type) {
	case BPF_PROG_TYPE_PERF_EVENT:
		return bpf_perf_link_fill_perf_event(event, info);
	case BPF_PROG_TYPE_TRACEPOINT:
		return bpf_perf_link_fill_tracepoint(event, info);
	case BPF_PROG_TYPE_KPROBE:
		return bpf_perf_link_fill_probe(event, info);
	default:
		return -EOPNOTSUPP;
	}
}

static const struct bpf_link_ops bpf_perf_link_lops = {
	.release = bpf_perf_link_release,
	.dealloc = bpf_perf_link_dealloc,
	.fill_link_info = bpf_perf_link_fill_link_info,
};

static int bpf_perf_link_attach(const union bpf_attr *attr, struct bpf_prog *prog)
{
	struct bpf_link_primer link_primer;
	struct bpf_perf_link *link;
	struct perf_event *event;
	struct file *perf_file;
	int err;

	if (attr->link_create.flags)
		return -EINVAL;

	perf_file = perf_event_get(attr->link_create.target_fd);
	if (IS_ERR(perf_file))
		return PTR_ERR(perf_file);

	link = kzalloc(sizeof(*link), GFP_USER);
	if (!link) {
		err = -ENOMEM;
		goto out_put_file;
	}
	bpf_link_init(&link->link, BPF_LINK_TYPE_PERF_EVENT, &bpf_perf_link_lops, prog);
	link->perf_file = perf_file;

	err = bpf_link_prime(&link->link, &link_primer);
	if (err) {
		kfree(link);
		goto out_put_file;
	}

	event = perf_file->private_data;
	err = perf_event_set_bpf_prog(event, prog, attr->link_create.perf_event.bpf_cookie);
	if (err) {
		bpf_link_cleanup(&link_primer);
		goto out_put_file;
	}
	/* perf_event_set_bpf_prog() doesn't take its own refcnt on prog */
	bpf_prog_inc(prog);

	return bpf_link_settle(&link_primer);

out_put_file:
	fput(perf_file);
	return err;
}
#else
static int bpf_perf_link_attach(const union bpf_attr *attr, struct bpf_prog *prog)
{
	return -EOPNOTSUPP;
}
#endif /* CONFIG_PERF_EVENTS */

static int bpf_raw_tp_link_attach(struct bpf_prog *prog,
				  const char __user *user_tp_name, u64 cookie)
{
	struct bpf_link_primer link_primer;
	struct bpf_raw_tp_link *link;
	struct bpf_raw_event_map *btp;
	const char *tp_name;
	char buf[128];
	int err;

	switch (prog->type) {
	case BPF_PROG_TYPE_TRACING:
	case BPF_PROG_TYPE_EXT:
	case BPF_PROG_TYPE_LSM:
		if (user_tp_name)
			/* The attach point for this category of programs
			 * should be specified via btf_id during program load.
			 */
			return -EINVAL;
		if (prog->type == BPF_PROG_TYPE_TRACING &&
		    prog->expected_attach_type == BPF_TRACE_RAW_TP) {
			tp_name = prog->aux->attach_func_name;
			break;
		}
		return bpf_tracing_prog_attach(prog, 0, 0, 0);
	case BPF_PROG_TYPE_RAW_TRACEPOINT:
	case BPF_PROG_TYPE_RAW_TRACEPOINT_WRITABLE:
		if (strncpy_from_user(buf, user_tp_name, sizeof(buf) - 1) < 0)
			return -EFAULT;
		buf[sizeof(buf) - 1] = 0;
		tp_name = buf;
		break;
	default:
		return -EINVAL;
	}

	btp = bpf_get_raw_tracepoint(tp_name);
	if (!btp)
		return -ENOENT;

	link = kzalloc(sizeof(*link), GFP_USER);
	if (!link) {
		err = -ENOMEM;
		goto out_put_btp;
	}
	bpf_link_init_sleepable(&link->link, BPF_LINK_TYPE_RAW_TRACEPOINT,
				&bpf_raw_tp_link_lops, prog,
				tracepoint_is_faultable(btp->tp));
	link->btp = btp;
	link->cookie = cookie;

	err = bpf_link_prime(&link->link, &link_primer);
	if (err) {
		kfree(link);
		goto out_put_btp;
	}

	err = bpf_probe_register(link->btp, link);
	if (err) {
		bpf_link_cleanup(&link_primer);
		goto out_put_btp;
	}

	return bpf_link_settle(&link_primer);

out_put_btp:
	bpf_put_raw_tracepoint(btp);
	return err;
}

#define BPF_RAW_TRACEPOINT_OPEN_LAST_FIELD raw_tracepoint.cookie

static int bpf_raw_tracepoint_open(const union bpf_attr *attr)
{
	struct bpf_prog *prog;
	void __user *tp_name;
	__u64 cookie;
	int fd;

	if (CHECK_ATTR(BPF_RAW_TRACEPOINT_OPEN))
		return -EINVAL;

	prog = bpf_prog_get(attr->raw_tracepoint.prog_fd);
	if (IS_ERR(prog))
		return PTR_ERR(prog);

	tp_name = u64_to_user_ptr(attr->raw_tracepoint.name);
	cookie = attr->raw_tracepoint.cookie;
	fd = bpf_raw_tp_link_attach(prog, tp_name, cookie);
	if (fd < 0)
		bpf_prog_put(prog);
	return fd;
}

static enum bpf_prog_type
attach_type_to_prog_type(enum bpf_attach_type attach_type)
{
	switch (attach_type) {
	case BPF_CGROUP_INET_INGRESS:
	case BPF_CGROUP_INET_EGRESS:
		return BPF_PROG_TYPE_CGROUP_SKB;
	case BPF_CGROUP_INET_SOCK_CREATE:
	case BPF_CGROUP_INET_SOCK_RELEASE:
	case BPF_CGROUP_INET4_POST_BIND:
	case BPF_CGROUP_INET6_POST_BIND:
		return BPF_PROG_TYPE_CGROUP_SOCK;
	case BPF_CGROUP_INET4_BIND:
	case BPF_CGROUP_INET6_BIND:
	case BPF_CGROUP_INET4_CONNECT:
	case BPF_CGROUP_INET6_CONNECT:
	case BPF_CGROUP_UNIX_CONNECT:
	case BPF_CGROUP_INET4_GETPEERNAME:
	case BPF_CGROUP_INET6_GETPEERNAME:
	case BPF_CGROUP_UNIX_GETPEERNAME:
	case BPF_CGROUP_INET4_GETSOCKNAME:
	case BPF_CGROUP_INET6_GETSOCKNAME:
	case BPF_CGROUP_UNIX_GETSOCKNAME:
	case BPF_CGROUP_UDP4_SENDMSG:
	case BPF_CGROUP_UDP6_SENDMSG:
	case BPF_CGROUP_UNIX_SENDMSG:
	case BPF_CGROUP_UDP4_RECVMSG:
	case BPF_CGROUP_UDP6_RECVMSG:
	case BPF_CGROUP_UNIX_RECVMSG:
		return BPF_PROG_TYPE_CGROUP_SOCK_ADDR;
	case BPF_CGROUP_SOCK_OPS:
		return BPF_PROG_TYPE_SOCK_OPS;
	case BPF_CGROUP_DEVICE:
		return BPF_PROG_TYPE_CGROUP_DEVICE;
	case BPF_SK_MSG_VERDICT:
		return BPF_PROG_TYPE_SK_MSG;
	case BPF_SK_SKB_STREAM_PARSER:
	case BPF_SK_SKB_STREAM_VERDICT:
	case BPF_SK_SKB_VERDICT:
		return BPF_PROG_TYPE_SK_SKB;
	case BPF_LIRC_MODE2:
		return BPF_PROG_TYPE_LIRC_MODE2;
	case BPF_FLOW_DISSECTOR:
		return BPF_PROG_TYPE_FLOW_DISSECTOR;
	case BPF_CGROUP_SYSCTL:
		return BPF_PROG_TYPE_CGROUP_SYSCTL;
	case BPF_CGROUP_GETSOCKOPT:
	case BPF_CGROUP_SETSOCKOPT:
		return BPF_PROG_TYPE_CGROUP_SOCKOPT;
	case BPF_TRACE_ITER:
	case BPF_TRACE_RAW_TP:
	case BPF_TRACE_FENTRY:
	case BPF_TRACE_FEXIT:
	case BPF_MODIFY_RETURN:
		return BPF_PROG_TYPE_TRACING;
	case BPF_LSM_MAC:
		return BPF_PROG_TYPE_LSM;
	case BPF_SK_LOOKUP:
		return BPF_PROG_TYPE_SK_LOOKUP;
	case BPF_XDP:
		return BPF_PROG_TYPE_XDP;
	case BPF_LSM_CGROUP:
		return BPF_PROG_TYPE_LSM;
	case BPF_TCX_INGRESS:
	case BPF_TCX_EGRESS:
	case BPF_NETKIT_PRIMARY:
	case BPF_NETKIT_PEER:
		return BPF_PROG_TYPE_SCHED_CLS;
	default:
		return BPF_PROG_TYPE_UNSPEC;
	}
}

static int bpf_prog_attach_check_attach_type(const struct bpf_prog *prog,
					     enum bpf_attach_type attach_type)
{
	enum bpf_prog_type ptype;

	switch (prog->type) {
	case BPF_PROG_TYPE_CGROUP_SOCK:
	case BPF_PROG_TYPE_CGROUP_SOCK_ADDR:
	case BPF_PROG_TYPE_CGROUP_SOCKOPT:
	case BPF_PROG_TYPE_SK_LOOKUP:
		return attach_type == prog->expected_attach_type ? 0 : -EINVAL;
	case BPF_PROG_TYPE_CGROUP_SKB:
		if (!bpf_token_capable(prog->aux->token, CAP_NET_ADMIN))
			/* cg-skb progs can be loaded by unpriv user.
			 * check permissions at attach time.
			 */
			return -EPERM;

		ptype = attach_type_to_prog_type(attach_type);
		if (prog->type != ptype)
			return -EINVAL;

		return prog->enforce_expected_attach_type &&
			prog->expected_attach_type != attach_type ?
			-EINVAL : 0;
	case BPF_PROG_TYPE_EXT:
		return 0;
	case BPF_PROG_TYPE_NETFILTER:
		if (attach_type != BPF_NETFILTER)
			return -EINVAL;
		return 0;
	case BPF_PROG_TYPE_PERF_EVENT:
	case BPF_PROG_TYPE_TRACEPOINT:
		if (attach_type != BPF_PERF_EVENT)
			return -EINVAL;
		return 0;
	case BPF_PROG_TYPE_KPROBE:
		if (prog->expected_attach_type == BPF_TRACE_KPROBE_MULTI &&
		    attach_type != BPF_TRACE_KPROBE_MULTI)
			return -EINVAL;
		if (prog->expected_attach_type == BPF_TRACE_KPROBE_SESSION &&
		    attach_type != BPF_TRACE_KPROBE_SESSION)
			return -EINVAL;
		if (prog->expected_attach_type == BPF_TRACE_UPROBE_MULTI &&
		    attach_type != BPF_TRACE_UPROBE_MULTI)
			return -EINVAL;
		if (prog->expected_attach_type == BPF_TRACE_UPROBE_SESSION &&
		    attach_type != BPF_TRACE_UPROBE_SESSION)
			return -EINVAL;
		if (attach_type != BPF_PERF_EVENT &&
		    attach_type != BPF_TRACE_KPROBE_MULTI &&
		    attach_type != BPF_TRACE_KPROBE_SESSION &&
		    attach_type != BPF_TRACE_UPROBE_MULTI &&
		    attach_type != BPF_TRACE_UPROBE_SESSION)
			return -EINVAL;
		return 0;
	case BPF_PROG_TYPE_SCHED_CLS:
		if (attach_type != BPF_TCX_INGRESS &&
		    attach_type != BPF_TCX_EGRESS &&
		    attach_type != BPF_NETKIT_PRIMARY &&
		    attach_type != BPF_NETKIT_PEER)
			return -EINVAL;
		return 0;
	default:
		ptype = attach_type_to_prog_type(attach_type);
		if (ptype == BPF_PROG_TYPE_UNSPEC || ptype != prog->type)
			return -EINVAL;
		return 0;
	}
}

#define BPF_PROG_ATTACH_LAST_FIELD expected_revision

#define BPF_F_ATTACH_MASK_BASE	\
	(BPF_F_ALLOW_OVERRIDE |	\
	 BPF_F_ALLOW_MULTI |	\
	 BPF_F_REPLACE |	\
	 BPF_F_PREORDER)

#define BPF_F_ATTACH_MASK_MPROG	\
	(BPF_F_REPLACE |	\
	 BPF_F_BEFORE |		\
	 BPF_F_AFTER |		\
	 BPF_F_ID |		\
	 BPF_F_LINK)

static int bpf_prog_attach(const union bpf_attr *attr)
{
	enum bpf_prog_type ptype;
	struct bpf_prog *prog;
	int ret;

	if (CHECK_ATTR(BPF_PROG_ATTACH))
		return -EINVAL;

	ptype = attach_type_to_prog_type(attr->attach_type);
	if (ptype == BPF_PROG_TYPE_UNSPEC)
		return -EINVAL;
	if (bpf_mprog_supported(ptype)) {
		if (attr->attach_flags & ~BPF_F_ATTACH_MASK_MPROG)
			return -EINVAL;
	} else {
		if (attr->attach_flags & ~BPF_F_ATTACH_MASK_BASE)
			return -EINVAL;
		if (attr->relative_fd ||
		    attr->expected_revision)
			return -EINVAL;
	}

	prog = bpf_prog_get_type(attr->attach_bpf_fd, ptype);
	if (IS_ERR(prog))
		return PTR_ERR(prog);

	if (bpf_prog_attach_check_attach_type(prog, attr->attach_type)) {
		bpf_prog_put(prog);
		return -EINVAL;
	}

	switch (ptype) {
	case BPF_PROG_TYPE_SK_SKB:
	case BPF_PROG_TYPE_SK_MSG:
		ret = sock_map_get_from_fd(attr, prog);
		break;
	case BPF_PROG_TYPE_LIRC_MODE2:
		ret = lirc_prog_attach(attr, prog);
		break;
	case BPF_PROG_TYPE_FLOW_DISSECTOR:
		ret = netns_bpf_prog_attach(attr, prog);
		break;
	case BPF_PROG_TYPE_CGROUP_DEVICE:
	case BPF_PROG_TYPE_CGROUP_SKB:
	case BPF_PROG_TYPE_CGROUP_SOCK:
	case BPF_PROG_TYPE_CGROUP_SOCK_ADDR:
	case BPF_PROG_TYPE_CGROUP_SOCKOPT:
	case BPF_PROG_TYPE_CGROUP_SYSCTL:
	case BPF_PROG_TYPE_SOCK_OPS:
	case BPF_PROG_TYPE_LSM:
		if (ptype == BPF_PROG_TYPE_LSM &&
		    prog->expected_attach_type != BPF_LSM_CGROUP)
			ret = -EINVAL;
		else
			ret = cgroup_bpf_prog_attach(attr, ptype, prog);
		break;
	case BPF_PROG_TYPE_SCHED_CLS:
		if (attr->attach_type == BPF_TCX_INGRESS ||
		    attr->attach_type == BPF_TCX_EGRESS)
			ret = tcx_prog_attach(attr, prog);
		else
			ret = netkit_prog_attach(attr, prog);
		break;
	default:
		ret = -EINVAL;
	}

	if (ret)
		bpf_prog_put(prog);
	return ret;
}

#define BPF_PROG_DETACH_LAST_FIELD expected_revision

static int bpf_prog_detach(const union bpf_attr *attr)
{
	struct bpf_prog *prog = NULL;
	enum bpf_prog_type ptype;
	int ret;

	if (CHECK_ATTR(BPF_PROG_DETACH))
		return -EINVAL;

	ptype = attach_type_to_prog_type(attr->attach_type);
	if (bpf_mprog_supported(ptype)) {
		if (ptype == BPF_PROG_TYPE_UNSPEC)
			return -EINVAL;
		if (attr->attach_flags & ~BPF_F_ATTACH_MASK_MPROG)
			return -EINVAL;
		if (attr->attach_bpf_fd) {
			prog = bpf_prog_get_type(attr->attach_bpf_fd, ptype);
			if (IS_ERR(prog))
				return PTR_ERR(prog);
		}
	} else if (attr->attach_flags ||
		   attr->relative_fd ||
		   attr->expected_revision) {
		return -EINVAL;
	}

	switch (ptype) {
	case BPF_PROG_TYPE_SK_MSG:
	case BPF_PROG_TYPE_SK_SKB:
		ret = sock_map_prog_detach(attr, ptype);
		break;
	case BPF_PROG_TYPE_LIRC_MODE2:
		ret = lirc_prog_detach(attr);
		break;
	case BPF_PROG_TYPE_FLOW_DISSECTOR:
		ret = netns_bpf_prog_detach(attr, ptype);
		break;
	case BPF_PROG_TYPE_CGROUP_DEVICE:
	case BPF_PROG_TYPE_CGROUP_SKB:
	case BPF_PROG_TYPE_CGROUP_SOCK:
	case BPF_PROG_TYPE_CGROUP_SOCK_ADDR:
	case BPF_PROG_TYPE_CGROUP_SOCKOPT:
	case BPF_PROG_TYPE_CGROUP_SYSCTL:
	case BPF_PROG_TYPE_SOCK_OPS:
	case BPF_PROG_TYPE_LSM:
		ret = cgroup_bpf_prog_detach(attr, ptype);
		break;
	case BPF_PROG_TYPE_SCHED_CLS:
		if (attr->attach_type == BPF_TCX_INGRESS ||
		    attr->attach_type == BPF_TCX_EGRESS)
			ret = tcx_prog_detach(attr, prog);
		else
			ret = netkit_prog_detach(attr, prog);
		break;
	default:
		ret = -EINVAL;
	}

	if (prog)
		bpf_prog_put(prog);
	return ret;
}

#define BPF_PROG_QUERY_LAST_FIELD query.revision

static int bpf_prog_query(const union bpf_attr *attr,
			  union bpf_attr __user *uattr)
{
	if (!bpf_net_capable())
		return -EPERM;
	if (CHECK_ATTR(BPF_PROG_QUERY))
		return -EINVAL;
	if (attr->query.query_flags & ~BPF_F_QUERY_EFFECTIVE)
		return -EINVAL;

	switch (attr->query.attach_type) {
	case BPF_CGROUP_INET_INGRESS:
	case BPF_CGROUP_INET_EGRESS:
	case BPF_CGROUP_INET_SOCK_CREATE:
	case BPF_CGROUP_INET_SOCK_RELEASE:
	case BPF_CGROUP_INET4_BIND:
	case BPF_CGROUP_INET6_BIND:
	case BPF_CGROUP_INET4_POST_BIND:
	case BPF_CGROUP_INET6_POST_BIND:
	case BPF_CGROUP_INET4_CONNECT:
	case BPF_CGROUP_INET6_CONNECT:
	case BPF_CGROUP_UNIX_CONNECT:
	case BPF_CGROUP_INET4_GETPEERNAME:
	case BPF_CGROUP_INET6_GETPEERNAME:
	case BPF_CGROUP_UNIX_GETPEERNAME:
	case BPF_CGROUP_INET4_GETSOCKNAME:
	case BPF_CGROUP_INET6_GETSOCKNAME:
	case BPF_CGROUP_UNIX_GETSOCKNAME:
	case BPF_CGROUP_UDP4_SENDMSG:
	case BPF_CGROUP_UDP6_SENDMSG:
	case BPF_CGROUP_UNIX_SENDMSG:
	case BPF_CGROUP_UDP4_RECVMSG:
	case BPF_CGROUP_UDP6_RECVMSG:
	case BPF_CGROUP_UNIX_RECVMSG:
	case BPF_CGROUP_SOCK_OPS:
	case BPF_CGROUP_DEVICE:
	case BPF_CGROUP_SYSCTL:
	case BPF_CGROUP_GETSOCKOPT:
	case BPF_CGROUP_SETSOCKOPT:
	case BPF_LSM_CGROUP:
		return cgroup_bpf_prog_query(attr, uattr);
	case BPF_LIRC_MODE2:
		return lirc_prog_query(attr, uattr);
	case BPF_FLOW_DISSECTOR:
	case BPF_SK_LOOKUP:
		return netns_bpf_prog_query(attr, uattr);
	case BPF_SK_SKB_STREAM_PARSER:
	case BPF_SK_SKB_STREAM_VERDICT:
	case BPF_SK_MSG_VERDICT:
	case BPF_SK_SKB_VERDICT:
		return sock_map_bpf_prog_query(attr, uattr);
	case BPF_TCX_INGRESS:
	case BPF_TCX_EGRESS:
		return tcx_prog_query(attr, uattr);
	case BPF_NETKIT_PRIMARY:
	case BPF_NETKIT_PEER:
		return netkit_prog_query(attr, uattr);
	default:
		return -EINVAL;
	}
}

#define BPF_PROG_TEST_RUN_LAST_FIELD test.batch_size

static int bpf_prog_test_run(const union bpf_attr *attr,
			     union bpf_attr __user *uattr)
{
	struct bpf_prog *prog;
	int ret = -ENOTSUPP;

	if (CHECK_ATTR(BPF_PROG_TEST_RUN))
		return -EINVAL;

	if ((attr->test.ctx_size_in && !attr->test.ctx_in) ||
	    (!attr->test.ctx_size_in && attr->test.ctx_in))
		return -EINVAL;

	if ((attr->test.ctx_size_out && !attr->test.ctx_out) ||
	    (!attr->test.ctx_size_out && attr->test.ctx_out))
		return -EINVAL;

	prog = bpf_prog_get(attr->test.prog_fd);
	if (IS_ERR(prog))
		return PTR_ERR(prog);

	if (prog->aux->ops->test_run)
		ret = prog->aux->ops->test_run(prog, attr, uattr);

	bpf_prog_put(prog);
	return ret;
}

#define BPF_OBJ_GET_NEXT_ID_LAST_FIELD next_id

static int bpf_obj_get_next_id(const union bpf_attr *attr,
			       union bpf_attr __user *uattr,
			       struct idr *idr,
			       spinlock_t *lock)
{
	u32 next_id = attr->start_id;
	int err = 0;

	if (CHECK_ATTR(BPF_OBJ_GET_NEXT_ID) || next_id >= INT_MAX)
		return -EINVAL;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	next_id++;
	spin_lock_bh(lock);
	if (!idr_get_next(idr, &next_id))
		err = -ENOENT;
	spin_unlock_bh(lock);

	if (!err)
		err = put_user(next_id, &uattr->next_id);

	return err;
}

struct bpf_map *bpf_map_get_curr_or_next(u32 *id)
{
	struct bpf_map *map;

	spin_lock_bh(&map_idr_lock);
again:
	map = idr_get_next(&map_idr, id);
	if (map) {
		map = __bpf_map_inc_not_zero(map, false);
		if (IS_ERR(map)) {
			(*id)++;
			goto again;
		}
	}
	spin_unlock_bh(&map_idr_lock);

	return map;
}

struct bpf_prog *bpf_prog_get_curr_or_next(u32 *id)
{
	struct bpf_prog *prog;

	spin_lock_bh(&prog_idr_lock);
again:
	prog = idr_get_next(&prog_idr, id);
	if (prog) {
		prog = bpf_prog_inc_not_zero(prog);
		if (IS_ERR(prog)) {
			(*id)++;
			goto again;
		}
	}
	spin_unlock_bh(&prog_idr_lock);

	return prog;
}

#define BPF_PROG_GET_FD_BY_ID_LAST_FIELD prog_id

struct bpf_prog *bpf_prog_by_id(u32 id)
{
	struct bpf_prog *prog;

	if (!id)
		return ERR_PTR(-ENOENT);

	spin_lock_bh(&prog_idr_lock);
	prog = idr_find(&prog_idr, id);
	if (prog)
		prog = bpf_prog_inc_not_zero(prog);
	else
		prog = ERR_PTR(-ENOENT);
	spin_unlock_bh(&prog_idr_lock);
	return prog;
}

static int bpf_prog_get_fd_by_id(const union bpf_attr *attr)
{
	struct bpf_prog *prog;
	u32 id = attr->prog_id;
	int fd;

	if (CHECK_ATTR(BPF_PROG_GET_FD_BY_ID))
		return -EINVAL;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	prog = bpf_prog_by_id(id);
	if (IS_ERR(prog))
		return PTR_ERR(prog);

	fd = bpf_prog_new_fd(prog);
	if (fd < 0)
		bpf_prog_put(prog);

	return fd;
}

#define BPF_MAP_GET_FD_BY_ID_LAST_FIELD open_flags

static int bpf_map_get_fd_by_id(const union bpf_attr *attr)
{
	struct bpf_map *map;
	u32 id = attr->map_id;
	int f_flags;
	int fd;

	if (CHECK_ATTR(BPF_MAP_GET_FD_BY_ID) ||
	    attr->open_flags & ~BPF_OBJ_FLAG_MASK)
		return -EINVAL;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	f_flags = bpf_get_file_flag(attr->open_flags);
	if (f_flags < 0)
		return f_flags;

	spin_lock_bh(&map_idr_lock);
	map = idr_find(&map_idr, id);
	if (map)
		map = __bpf_map_inc_not_zero(map, true);
	else
		map = ERR_PTR(-ENOENT);
	spin_unlock_bh(&map_idr_lock);

	if (IS_ERR(map))
		return PTR_ERR(map);

	fd = bpf_map_new_fd(map, f_flags);
	if (fd < 0)
		bpf_map_put_with_uref(map);

	return fd;
}

static const struct bpf_map *bpf_map_from_imm(const struct bpf_prog *prog,
					      unsigned long addr, u32 *off,
					      u32 *type)
{
	const struct bpf_map *map;
	int i;

	mutex_lock(&prog->aux->used_maps_mutex);
	for (i = 0, *off = 0; i < prog->aux->used_map_cnt; i++) {
		map = prog->aux->used_maps[i];
		if (map == (void *)addr) {
			*type = BPF_PSEUDO_MAP_FD;
			goto out;
		}
		if (!map->ops->map_direct_value_meta)
			continue;
		if (!map->ops->map_direct_value_meta(map, addr, off)) {
			*type = BPF_PSEUDO_MAP_VALUE;
			goto out;
		}
	}
	map = NULL;

out:
	mutex_unlock(&prog->aux->used_maps_mutex);
	return map;
}

static struct bpf_insn *bpf_insn_prepare_dump(const struct bpf_prog *prog,
					      const struct cred *f_cred)
{
	const struct bpf_map *map;
	struct bpf_insn *insns;
	u32 off, type;
	u64 imm;
	u8 code;
	int i;

	insns = kmemdup(prog->insnsi, bpf_prog_insn_size(prog),
			GFP_USER);
	if (!insns)
		return insns;

	for (i = 0; i < prog->len; i++) {
		code = insns[i].code;

		if (code == (BPF_JMP | BPF_TAIL_CALL)) {
			insns[i].code = BPF_JMP | BPF_CALL;
			insns[i].imm = BPF_FUNC_tail_call;
			/* fall-through */
		}
		if (code == (BPF_JMP | BPF_CALL) ||
		    code == (BPF_JMP | BPF_CALL_ARGS)) {
			if (code == (BPF_JMP | BPF_CALL_ARGS))
				insns[i].code = BPF_JMP | BPF_CALL;
			if (!bpf_dump_raw_ok(f_cred))
				insns[i].imm = 0;
			continue;
		}
		if (BPF_CLASS(code) == BPF_LDX && BPF_MODE(code) == BPF_PROBE_MEM) {
			insns[i].code = BPF_LDX | BPF_SIZE(code) | BPF_MEM;
			continue;
		}

		if ((BPF_CLASS(code) == BPF_LDX || BPF_CLASS(code) == BPF_STX ||
		     BPF_CLASS(code) == BPF_ST) && BPF_MODE(code) == BPF_PROBE_MEM32) {
			insns[i].code = BPF_CLASS(code) | BPF_SIZE(code) | BPF_MEM;
			continue;
		}

		if (code != (BPF_LD | BPF_IMM | BPF_DW))
			continue;

		imm = ((u64)insns[i + 1].imm << 32) | (u32)insns[i].imm;
		map = bpf_map_from_imm(prog, imm, &off, &type);
		if (map) {
			insns[i].src_reg = type;
			insns[i].imm = map->id;
			insns[i + 1].imm = off;
			continue;
		}
	}

	return insns;
}

static int set_info_rec_size(struct bpf_prog_info *info)
{
	/*
	 * Ensure info.*_rec_size is the same as kernel expected size
	 *
	 * or
	 *
	 * Only allow zero *_rec_size if both _rec_size and _cnt are
	 * zero.  In this case, the kernel will set the expected
	 * _rec_size back to the info.
	 */

	if ((info->nr_func_info || info->func_info_rec_size) &&
	    info->func_info_rec_size != sizeof(struct bpf_func_info))
		return -EINVAL;

	if ((info->nr_line_info || info->line_info_rec_size) &&
	    info->line_info_rec_size != sizeof(struct bpf_line_info))
		return -EINVAL;

	if ((info->nr_jited_line_info || info->jited_line_info_rec_size) &&
	    info->jited_line_info_rec_size != sizeof(__u64))
		return -EINVAL;

	info->func_info_rec_size = sizeof(struct bpf_func_info);
	info->line_info_rec_size = sizeof(struct bpf_line_info);
	info->jited_line_info_rec_size = sizeof(__u64);

	return 0;
}

static int bpf_prog_get_info_by_fd(struct file *file,
				   struct bpf_prog *prog,
				   const union bpf_attr *attr,
				   union bpf_attr __user *uattr)
{
	struct bpf_prog_info __user *uinfo = u64_to_user_ptr(attr->info.info);
	struct btf *attach_btf = bpf_prog_get_target_btf(prog);
	struct bpf_prog_info info;
	u32 info_len = attr->info.info_len;
	struct bpf_prog_kstats stats;
	char __user *uinsns;
	u32 ulen;
	int err;

	err = bpf_check_uarg_tail_zero(USER_BPFPTR(uinfo), sizeof(info), info_len);
	if (err)
		return err;
	info_len = min_t(u32, sizeof(info), info_len);

	memset(&info, 0, sizeof(info));
	if (copy_from_user(&info, uinfo, info_len))
		return -EFAULT;

	info.type = prog->type;
	info.id = prog->aux->id;
	info.load_time = prog->aux->load_time;
	info.created_by_uid = from_kuid_munged(current_user_ns(),
					       prog->aux->user->uid);
	info.gpl_compatible = prog->gpl_compatible;

	memcpy(info.tag, prog->tag, sizeof(prog->tag));
	memcpy(info.name, prog->aux->name, sizeof(prog->aux->name));

	mutex_lock(&prog->aux->used_maps_mutex);
	ulen = info.nr_map_ids;
	info.nr_map_ids = prog->aux->used_map_cnt;
	ulen = min_t(u32, info.nr_map_ids, ulen);
	if (ulen) {
		u32 __user *user_map_ids = u64_to_user_ptr(info.map_ids);
		u32 i;

		for (i = 0; i < ulen; i++)
			if (put_user(prog->aux->used_maps[i]->id,
				     &user_map_ids[i])) {
				mutex_unlock(&prog->aux->used_maps_mutex);
				return -EFAULT;
			}
	}
	mutex_unlock(&prog->aux->used_maps_mutex);

	err = set_info_rec_size(&info);
	if (err)
		return err;

	bpf_prog_get_stats(prog, &stats);
	info.run_time_ns = stats.nsecs;
	info.run_cnt = stats.cnt;
	info.recursion_misses = stats.misses;

	info.verified_insns = prog->aux->verified_insns;
	if (prog->aux->btf)
		info.btf_id = btf_obj_id(prog->aux->btf);

	if (!bpf_capable()) {
		info.jited_prog_len = 0;
		info.xlated_prog_len = 0;
		info.nr_jited_ksyms = 0;
		info.nr_jited_func_lens = 0;
		info.nr_func_info = 0;
		info.nr_line_info = 0;
		info.nr_jited_line_info = 0;
		goto done;
	}

	ulen = info.xlated_prog_len;
	info.xlated_prog_len = bpf_prog_insn_size(prog);
	if (info.xlated_prog_len && ulen) {
		struct bpf_insn *insns_sanitized;
		bool fault;

		if (prog->blinded && !bpf_dump_raw_ok(file->f_cred)) {
			info.xlated_prog_insns = 0;
			goto done;
		}
		insns_sanitized = bpf_insn_prepare_dump(prog, file->f_cred);
		if (!insns_sanitized)
			return -ENOMEM;
		uinsns = u64_to_user_ptr(info.xlated_prog_insns);
		ulen = min_t(u32, info.xlated_prog_len, ulen);
		fault = copy_to_user(uinsns, insns_sanitized, ulen);
		kfree(insns_sanitized);
		if (fault)
			return -EFAULT;
	}

	if (bpf_prog_is_offloaded(prog->aux)) {
		err = bpf_prog_offload_info_fill(&info, prog);
		if (err)
			return err;
		goto done;
	}

	/* NOTE: the following code is supposed to be skipped for offload.
	 * bpf_prog_offload_info_fill() is the place to fill similar fields
	 * for offload.
	 */
	ulen = info.jited_prog_len;
	if (prog->aux->func_cnt) {
		u32 i;

		info.jited_prog_len = 0;
		for (i = 0; i < prog->aux->func_cnt; i++)
			info.jited_prog_len += prog->aux->func[i]->jited_len;
	} else {
		info.jited_prog_len = prog->jited_len;
	}

	if (info.jited_prog_len && ulen) {
		if (bpf_dump_raw_ok(file->f_cred)) {
			uinsns = u64_to_user_ptr(info.jited_prog_insns);
			ulen = min_t(u32, info.jited_prog_len, ulen);

			/* for multi-function programs, copy the JITed
			 * instructions for all the functions
			 */
			if (prog->aux->func_cnt) {
				u32 len, free, i;
				u8 *img;

				free = ulen;
				for (i = 0; i < prog->aux->func_cnt; i++) {
					len = prog->aux->func[i]->jited_len;
					len = min_t(u32, len, free);
					img = (u8 *) prog->aux->func[i]->bpf_func;
					if (copy_to_user(uinsns, img, len))
						return -EFAULT;
					uinsns += len;
					free -= len;
					if (!free)
						break;
				}
			} else {
				if (copy_to_user(uinsns, prog->bpf_func, ulen))
					return -EFAULT;
			}
		} else {
			info.jited_prog_insns = 0;
		}
	}

	ulen = info.nr_jited_ksyms;
	info.nr_jited_ksyms = prog->aux->func_cnt ? : 1;
	if (ulen) {
		if (bpf_dump_raw_ok(file->f_cred)) {
			unsigned long ksym_addr;
			u64 __user *user_ksyms;
			u32 i;

			/* copy the address of the kernel symbol
			 * corresponding to each function
			 */
			ulen = min_t(u32, info.nr_jited_ksyms, ulen);
			user_ksyms = u64_to_user_ptr(info.jited_ksyms);
			if (prog->aux->func_cnt) {
				for (i = 0; i < ulen; i++) {
					ksym_addr = (unsigned long)
						prog->aux->func[i]->bpf_func;
					if (put_user((u64) ksym_addr,
						     &user_ksyms[i]))
						return -EFAULT;
				}
			} else {
				ksym_addr = (unsigned long) prog->bpf_func;
				if (put_user((u64) ksym_addr, &user_ksyms[0]))
					return -EFAULT;
			}
		} else {
			info.jited_ksyms = 0;
		}
	}

	ulen = info.nr_jited_func_lens;
	info.nr_jited_func_lens = prog->aux->func_cnt ? : 1;
	if (ulen) {
		if (bpf_dump_raw_ok(file->f_cred)) {
			u32 __user *user_lens;
			u32 func_len, i;

			/* copy the JITed image lengths for each function */
			ulen = min_t(u32, info.nr_jited_func_lens, ulen);
			user_lens = u64_to_user_ptr(info.jited_func_lens);
			if (prog->aux->func_cnt) {
				for (i = 0; i < ulen; i++) {
					func_len =
						prog->aux->func[i]->jited_len;
					if (put_user(func_len, &user_lens[i]))
						return -EFAULT;
				}
			} else {
				func_len = prog->jited_len;
				if (put_user(func_len, &user_lens[0]))
					return -EFAULT;
			}
		} else {
			info.jited_func_lens = 0;
		}
	}

	info.attach_btf_id = prog->aux->attach_btf_id;
	if (attach_btf)
		info.attach_btf_obj_id = btf_obj_id(attach_btf);

	ulen = info.nr_func_info;
	info.nr_func_info = prog->aux->func_info_cnt;
	if (info.nr_func_info && ulen) {
		char __user *user_finfo;

		user_finfo = u64_to_user_ptr(info.func_info);
		ulen = min_t(u32, info.nr_func_info, ulen);
		if (copy_to_user(user_finfo, prog->aux->func_info,
				 info.func_info_rec_size * ulen))
			return -EFAULT;
	}

	ulen = info.nr_line_info;
	info.nr_line_info = prog->aux->nr_linfo;
	if (info.nr_line_info && ulen) {
		__u8 __user *user_linfo;

		user_linfo = u64_to_user_ptr(info.line_info);
		ulen = min_t(u32, info.nr_line_info, ulen);
		if (copy_to_user(user_linfo, prog->aux->linfo,
				 info.line_info_rec_size * ulen))
			return -EFAULT;
	}

	ulen = info.nr_jited_line_info;
	if (prog->aux->jited_linfo)
		info.nr_jited_line_info = prog->aux->nr_linfo;
	else
		info.nr_jited_line_info = 0;
	if (info.nr_jited_line_info && ulen) {
		if (bpf_dump_raw_ok(file->f_cred)) {
			unsigned long line_addr;
			__u64 __user *user_linfo;
			u32 i;

			user_linfo = u64_to_user_ptr(info.jited_line_info);
			ulen = min_t(u32, info.nr_jited_line_info, ulen);
			for (i = 0; i < ulen; i++) {
				line_addr = (unsigned long)prog->aux->jited_linfo[i];
				if (put_user((__u64)line_addr, &user_linfo[i]))
					return -EFAULT;
			}
		} else {
			info.jited_line_info = 0;
		}
	}

	ulen = info.nr_prog_tags;
	info.nr_prog_tags = prog->aux->func_cnt ? : 1;
	if (ulen) {
		__u8 __user (*user_prog_tags)[BPF_TAG_SIZE];
		u32 i;

		user_prog_tags = u64_to_user_ptr(info.prog_tags);
		ulen = min_t(u32, info.nr_prog_tags, ulen);
		if (prog->aux->func_cnt) {
			for (i = 0; i < ulen; i++) {
				if (copy_to_user(user_prog_tags[i],
						 prog->aux->func[i]->tag,
						 BPF_TAG_SIZE))
					return -EFAULT;
			}
		} else {
			if (copy_to_user(user_prog_tags[0],
					 prog->tag, BPF_TAG_SIZE))
				return -EFAULT;
		}
	}

done:
	if (copy_to_user(uinfo, &info, info_len) ||
	    put_user(info_len, &uattr->info.info_len))
		return -EFAULT;

	return 0;
}

static int bpf_map_get_info_by_fd(struct file *file,
				  struct bpf_map *map,
				  const union bpf_attr *attr,
				  union bpf_attr __user *uattr)
{
	struct bpf_map_info __user *uinfo = u64_to_user_ptr(attr->info.info);
	struct bpf_map_info info;
	u32 info_len = attr->info.info_len;
	int err;

	err = bpf_check_uarg_tail_zero(USER_BPFPTR(uinfo), sizeof(info), info_len);
	if (err)
		return err;
	info_len = min_t(u32, sizeof(info), info_len);

	memset(&info, 0, sizeof(info));
	info.type = map->map_type;
	info.id = map->id;
	info.key_size = map->key_size;
	info.value_size = map->value_size;
	info.max_entries = map->max_entries;
	info.map_flags = map->map_flags;
	info.map_extra = map->map_extra;
	memcpy(info.name, map->name, sizeof(map->name));

	if (map->btf) {
		info.btf_id = btf_obj_id(map->btf);
		info.btf_key_type_id = map->btf_key_type_id;
		info.btf_value_type_id = map->btf_value_type_id;
	}
	info.btf_vmlinux_value_type_id = map->btf_vmlinux_value_type_id;
	if (map->map_type == BPF_MAP_TYPE_STRUCT_OPS)
		bpf_map_struct_ops_info_fill(&info, map);

	if (bpf_map_is_offloaded(map)) {
		err = bpf_map_offload_info_fill(&info, map);
		if (err)
			return err;
	}

	if (copy_to_user(uinfo, &info, info_len) ||
	    put_user(info_len, &uattr->info.info_len))
		return -EFAULT;

	return 0;
}

static int bpf_btf_get_info_by_fd(struct file *file,
				  struct btf *btf,
				  const union bpf_attr *attr,
				  union bpf_attr __user *uattr)
{
	struct bpf_btf_info __user *uinfo = u64_to_user_ptr(attr->info.info);
	u32 info_len = attr->info.info_len;
	int err;

	err = bpf_check_uarg_tail_zero(USER_BPFPTR(uinfo), sizeof(*uinfo), info_len);
	if (err)
		return err;

	return btf_get_info_by_fd(btf, attr, uattr);
}

static int bpf_link_get_info_by_fd(struct file *file,
				  struct bpf_link *link,
				  const union bpf_attr *attr,
				  union bpf_attr __user *uattr)
{
	struct bpf_link_info __user *uinfo = u64_to_user_ptr(attr->info.info);
	struct bpf_link_info info;
	u32 info_len = attr->info.info_len;
	int err;

	err = bpf_check_uarg_tail_zero(USER_BPFPTR(uinfo), sizeof(info), info_len);
	if (err)
		return err;
	info_len = min_t(u32, sizeof(info), info_len);

	memset(&info, 0, sizeof(info));
	if (copy_from_user(&info, uinfo, info_len))
		return -EFAULT;

	info.type = link->type;
	info.id = link->id;
	if (link->prog)
		info.prog_id = link->prog->aux->id;

	if (link->ops->fill_link_info) {
		err = link->ops->fill_link_info(link, &info);
		if (err)
			return err;
	}

	if (copy_to_user(uinfo, &info, info_len) ||
	    put_user(info_len, &uattr->info.info_len))
		return -EFAULT;

	return 0;
}


#define BPF_OBJ_GET_INFO_BY_FD_LAST_FIELD info.info

static int bpf_obj_get_info_by_fd(const union bpf_attr *attr,
				  union bpf_attr __user *uattr)
{
	if (CHECK_ATTR(BPF_OBJ_GET_INFO_BY_FD))
		return -EINVAL;

	CLASS(fd, f)(attr->info.bpf_fd);
	if (fd_empty(f))
		return -EBADFD;

	if (fd_file(f)->f_op == &bpf_prog_fops)
		return bpf_prog_get_info_by_fd(fd_file(f), fd_file(f)->private_data, attr,
					      uattr);
	else if (fd_file(f)->f_op == &bpf_map_fops)
		return bpf_map_get_info_by_fd(fd_file(f), fd_file(f)->private_data, attr,
					     uattr);
	else if (fd_file(f)->f_op == &btf_fops)
		return bpf_btf_get_info_by_fd(fd_file(f), fd_file(f)->private_data, attr, uattr);
	else if (fd_file(f)->f_op == &bpf_link_fops || fd_file(f)->f_op == &bpf_link_fops_poll)
		return bpf_link_get_info_by_fd(fd_file(f), fd_file(f)->private_data,
					      attr, uattr);
	return -EINVAL;
}

#define BPF_BTF_LOAD_LAST_FIELD btf_token_fd

static int bpf_btf_load(const union bpf_attr *attr, bpfptr_t uattr, __u32 uattr_size)
{
	struct bpf_token *token = NULL;

	if (CHECK_ATTR(BPF_BTF_LOAD))
		return -EINVAL;

	if (attr->btf_flags & ~BPF_F_TOKEN_FD)
		return -EINVAL;

	if (attr->btf_flags & BPF_F_TOKEN_FD) {
		token = bpf_token_get_from_fd(attr->btf_token_fd);
		if (IS_ERR(token))
			return PTR_ERR(token);
		if (!bpf_token_allow_cmd(token, BPF_BTF_LOAD)) {
			bpf_token_put(token);
			token = NULL;
		}
	}

	if (!bpf_token_capable(token, CAP_BPF)) {
		bpf_token_put(token);
		return -EPERM;
	}

	bpf_token_put(token);

	return btf_new_fd(attr, uattr, uattr_size);
}

#define BPF_BTF_GET_FD_BY_ID_LAST_FIELD fd_by_id_token_fd

static int bpf_btf_get_fd_by_id(const union bpf_attr *attr)
{
	struct bpf_token *token = NULL;

	if (CHECK_ATTR(BPF_BTF_GET_FD_BY_ID))
		return -EINVAL;

	if (attr->open_flags & ~BPF_F_TOKEN_FD)
		return -EINVAL;

	if (attr->open_flags & BPF_F_TOKEN_FD) {
		token = bpf_token_get_from_fd(attr->fd_by_id_token_fd);
		if (IS_ERR(token))
			return PTR_ERR(token);
		if (!bpf_token_allow_cmd(token, BPF_BTF_GET_FD_BY_ID)) {
			bpf_token_put(token);
			token = NULL;
		}
	}

	if (!bpf_token_capable(token, CAP_SYS_ADMIN)) {
		bpf_token_put(token);
		return -EPERM;
	}

	bpf_token_put(token);

	return btf_get_fd_by_id(attr->btf_id);
}

static int bpf_task_fd_query_copy(const union bpf_attr *attr,
				    union bpf_attr __user *uattr,
				    u32 prog_id, u32 fd_type,
				    const char *buf, u64 probe_offset,
				    u64 probe_addr)
{
	char __user *ubuf = u64_to_user_ptr(attr->task_fd_query.buf);
	u32 len = buf ? strlen(buf) : 0, input_len;
	int err = 0;

	if (put_user(len, &uattr->task_fd_query.buf_len))
		return -EFAULT;
	input_len = attr->task_fd_query.buf_len;
	if (input_len && ubuf) {
		if (!len) {
			/* nothing to copy, just make ubuf NULL terminated */
			char zero = '\0';

			if (put_user(zero, ubuf))
				return -EFAULT;
		} else if (input_len >= len + 1) {
			/* ubuf can hold the string with NULL terminator */
			if (copy_to_user(ubuf, buf, len + 1))
				return -EFAULT;
		} else {
			/* ubuf cannot hold the string with NULL terminator,
			 * do a partial copy with NULL terminator.
			 */
			char zero = '\0';

			err = -ENOSPC;
			if (copy_to_user(ubuf, buf, input_len - 1))
				return -EFAULT;
			if (put_user(zero, ubuf + input_len - 1))
				return -EFAULT;
		}
	}

	if (put_user(prog_id, &uattr->task_fd_query.prog_id) ||
	    put_user(fd_type, &uattr->task_fd_query.fd_type) ||
	    put_user(probe_offset, &uattr->task_fd_query.probe_offset) ||
	    put_user(probe_addr, &uattr->task_fd_query.probe_addr))
		return -EFAULT;

	return err;
}

#define BPF_TASK_FD_QUERY_LAST_FIELD task_fd_query.probe_addr

static int bpf_task_fd_query(const union bpf_attr *attr,
			     union bpf_attr __user *uattr)
{
	pid_t pid = attr->task_fd_query.pid;
	u32 fd = attr->task_fd_query.fd;
	const struct perf_event *event;
	struct task_struct *task;
	struct file *file;
	int err;

	if (CHECK_ATTR(BPF_TASK_FD_QUERY))
		return -EINVAL;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (attr->task_fd_query.flags != 0)
		return -EINVAL;

	rcu_read_lock();
	task = get_pid_task(find_vpid(pid), PIDTYPE_PID);
	rcu_read_unlock();
	if (!task)
		return -ENOENT;

	err = 0;
	file = fget_task(task, fd);
	put_task_struct(task);
	if (!file)
		return -EBADF;

	if (file->f_op == &bpf_link_fops || file->f_op == &bpf_link_fops_poll) {
		struct bpf_link *link = file->private_data;

		if (link->ops == &bpf_raw_tp_link_lops) {
			struct bpf_raw_tp_link *raw_tp =
				container_of(link, struct bpf_raw_tp_link, link);
			struct bpf_raw_event_map *btp = raw_tp->btp;

			err = bpf_task_fd_query_copy(attr, uattr,
						     raw_tp->link.prog->aux->id,
						     BPF_FD_TYPE_RAW_TRACEPOINT,
						     btp->tp->name, 0, 0);
			goto put_file;
		}
		goto out_not_supp;
	}

	event = perf_get_event(file);
	if (!IS_ERR(event)) {
		u64 probe_offset, probe_addr;
		u32 prog_id, fd_type;
		const char *buf;

		err = bpf_get_perf_event_info(event, &prog_id, &fd_type,
					      &buf, &probe_offset,
					      &probe_addr, NULL);
		if (!err)
			err = bpf_task_fd_query_copy(attr, uattr, prog_id,
						     fd_type, buf,
						     probe_offset,
						     probe_addr);
		goto put_file;
	}

out_not_supp:
	err = -ENOTSUPP;
put_file:
	fput(file);
	return err;
}

#define BPF_MAP_BATCH_LAST_FIELD batch.flags

#define BPF_DO_BATCH(fn, ...)			\
	do {					\
		if (!fn) {			\
			err = -ENOTSUPP;	\
			goto err_put;		\
		}				\
		err = fn(__VA_ARGS__);		\
	} while (0)

static int bpf_map_do_batch(const union bpf_attr *attr,
			    union bpf_attr __user *uattr,
			    int cmd)
{
	bool has_read  = cmd == BPF_MAP_LOOKUP_BATCH ||
			 cmd == BPF_MAP_LOOKUP_AND_DELETE_BATCH;
	bool has_write = cmd != BPF_MAP_LOOKUP_BATCH;
	struct bpf_map *map;
	int err;

	if (CHECK_ATTR(BPF_MAP_BATCH))
		return -EINVAL;

	CLASS(fd, f)(attr->batch.map_fd);

	map = __bpf_map_get(f);
	if (IS_ERR(map))
		return PTR_ERR(map);
	if (has_write)
		bpf_map_write_active_inc(map);
	if (has_read && !(map_get_sys_perms(map, f) & FMODE_CAN_READ)) {
		err = -EPERM;
		goto err_put;
	}
	if (has_write && !(map_get_sys_perms(map, f) & FMODE_CAN_WRITE)) {
		err = -EPERM;
		goto err_put;
	}

	if (cmd == BPF_MAP_LOOKUP_BATCH)
		BPF_DO_BATCH(map->ops->map_lookup_batch, map, attr, uattr);
	else if (cmd == BPF_MAP_LOOKUP_AND_DELETE_BATCH)
		BPF_DO_BATCH(map->ops->map_lookup_and_delete_batch, map, attr, uattr);
	else if (cmd == BPF_MAP_UPDATE_BATCH)
		BPF_DO_BATCH(map->ops->map_update_batch, map, fd_file(f), attr, uattr);
	else
		BPF_DO_BATCH(map->ops->map_delete_batch, map, attr, uattr);
err_put:
	if (has_write) {
		maybe_wait_bpf_programs(map);
		bpf_map_write_active_dec(map);
	}
	return err;
}

#define BPF_LINK_CREATE_LAST_FIELD link_create.uprobe_multi.pid
static int link_create(union bpf_attr *attr, bpfptr_t uattr)
{
	struct bpf_prog *prog;
	int ret;

	if (CHECK_ATTR(BPF_LINK_CREATE))
		return -EINVAL;

	if (attr->link_create.attach_type == BPF_STRUCT_OPS)
		return bpf_struct_ops_link_create(attr);

	prog = bpf_prog_get(attr->link_create.prog_fd);
	if (IS_ERR(prog))
		return PTR_ERR(prog);

	ret = bpf_prog_attach_check_attach_type(prog,
						attr->link_create.attach_type);
	if (ret)
		goto out;

	switch (prog->type) {
	case BPF_PROG_TYPE_CGROUP_SKB:
	case BPF_PROG_TYPE_CGROUP_SOCK:
	case BPF_PROG_TYPE_CGROUP_SOCK_ADDR:
	case BPF_PROG_TYPE_SOCK_OPS:
	case BPF_PROG_TYPE_CGROUP_DEVICE:
	case BPF_PROG_TYPE_CGROUP_SYSCTL:
	case BPF_PROG_TYPE_CGROUP_SOCKOPT:
		ret = cgroup_bpf_link_attach(attr, prog);
		break;
	case BPF_PROG_TYPE_EXT:
		ret = bpf_tracing_prog_attach(prog,
					      attr->link_create.target_fd,
					      attr->link_create.target_btf_id,
					      attr->link_create.tracing.cookie);
		break;
	case BPF_PROG_TYPE_LSM:
	case BPF_PROG_TYPE_TRACING:
		if (attr->link_create.attach_type != prog->expected_attach_type) {
			ret = -EINVAL;
			goto out;
		}
		if (prog->expected_attach_type == BPF_TRACE_RAW_TP)
			ret = bpf_raw_tp_link_attach(prog, NULL, attr->link_create.tracing.cookie);
		else if (prog->expected_attach_type == BPF_TRACE_ITER)
			ret = bpf_iter_link_attach(attr, uattr, prog);
		else if (prog->expected_attach_type == BPF_LSM_CGROUP)
			ret = cgroup_bpf_link_attach(attr, prog);
		else
			ret = bpf_tracing_prog_attach(prog,
						      attr->link_create.target_fd,
						      attr->link_create.target_btf_id,
						      attr->link_create.tracing.cookie);
		break;
	case BPF_PROG_TYPE_FLOW_DISSECTOR:
	case BPF_PROG_TYPE_SK_LOOKUP:
		ret = netns_bpf_link_create(attr, prog);
		break;
	case BPF_PROG_TYPE_SK_MSG:
	case BPF_PROG_TYPE_SK_SKB:
		ret = sock_map_link_create(attr, prog);
		break;
#ifdef CONFIG_NET
	case BPF_PROG_TYPE_XDP:
		ret = bpf_xdp_link_attach(attr, prog);
		break;
	case BPF_PROG_TYPE_SCHED_CLS:
		if (attr->link_create.attach_type == BPF_TCX_INGRESS ||
		    attr->link_create.attach_type == BPF_TCX_EGRESS)
			ret = tcx_link_attach(attr, prog);
		else
			ret = netkit_link_attach(attr, prog);
		break;
	case BPF_PROG_TYPE_NETFILTER:
		ret = bpf_nf_link_attach(attr, prog);
		break;
#endif
	case BPF_PROG_TYPE_PERF_EVENT:
	case BPF_PROG_TYPE_TRACEPOINT:
		ret = bpf_perf_link_attach(attr, prog);
		break;
	case BPF_PROG_TYPE_KPROBE:
		if (attr->link_create.attach_type == BPF_PERF_EVENT)
			ret = bpf_perf_link_attach(attr, prog);
		else if (attr->link_create.attach_type == BPF_TRACE_KPROBE_MULTI ||
			 attr->link_create.attach_type == BPF_TRACE_KPROBE_SESSION)
			ret = bpf_kprobe_multi_link_attach(attr, prog);
		else if (attr->link_create.attach_type == BPF_TRACE_UPROBE_MULTI ||
			 attr->link_create.attach_type == BPF_TRACE_UPROBE_SESSION)
			ret = bpf_uprobe_multi_link_attach(attr, prog);
		break;
	default:
		ret = -EINVAL;
	}

out:
	if (ret < 0)
		bpf_prog_put(prog);
	return ret;
}

static int link_update_map(struct bpf_link *link, union bpf_attr *attr)
{
	struct bpf_map *new_map, *old_map = NULL;
	int ret;

	new_map = bpf_map_get(attr->link_update.new_map_fd);
	if (IS_ERR(new_map))
		return PTR_ERR(new_map);

	if (attr->link_update.flags & BPF_F_REPLACE) {
		old_map = bpf_map_get(attr->link_update.old_map_fd);
		if (IS_ERR(old_map)) {
			ret = PTR_ERR(old_map);
			goto out_put;
		}
	} else if (attr->link_update.old_map_fd) {
		ret = -EINVAL;
		goto out_put;
	}

	ret = link->ops->update_map(link, new_map, old_map);

	if (old_map)
		bpf_map_put(old_map);
out_put:
	bpf_map_put(new_map);
	return ret;
}

#define BPF_LINK_UPDATE_LAST_FIELD link_update.old_prog_fd

static int link_update(union bpf_attr *attr)
{
	struct bpf_prog *old_prog = NULL, *new_prog;
	struct bpf_link *link;
	u32 flags;
	int ret;

	if (CHECK_ATTR(BPF_LINK_UPDATE))
		return -EINVAL;

	flags = attr->link_update.flags;
	if (flags & ~BPF_F_REPLACE)
		return -EINVAL;

	link = bpf_link_get_from_fd(attr->link_update.link_fd);
	if (IS_ERR(link))
		return PTR_ERR(link);

	if (link->ops->update_map) {
		ret = link_update_map(link, attr);
		goto out_put_link;
	}

	new_prog = bpf_prog_get(attr->link_update.new_prog_fd);
	if (IS_ERR(new_prog)) {
		ret = PTR_ERR(new_prog);
		goto out_put_link;
	}

	if (flags & BPF_F_REPLACE) {
		old_prog = bpf_prog_get(attr->link_update.old_prog_fd);
		if (IS_ERR(old_prog)) {
			ret = PTR_ERR(old_prog);
			old_prog = NULL;
			goto out_put_progs;
		}
	} else if (attr->link_update.old_prog_fd) {
		ret = -EINVAL;
		goto out_put_progs;
	}

	if (link->ops->update_prog)
		ret = link->ops->update_prog(link, new_prog, old_prog);
	else
		ret = -EINVAL;

out_put_progs:
	if (old_prog)
		bpf_prog_put(old_prog);
	if (ret)
		bpf_prog_put(new_prog);
out_put_link:
	bpf_link_put_direct(link);
	return ret;
}

#define BPF_LINK_DETACH_LAST_FIELD link_detach.link_fd

static int link_detach(union bpf_attr *attr)
{
	struct bpf_link *link;
	int ret;

	if (CHECK_ATTR(BPF_LINK_DETACH))
		return -EINVAL;

	link = bpf_link_get_from_fd(attr->link_detach.link_fd);
	if (IS_ERR(link))
		return PTR_ERR(link);

	if (link->ops->detach)
		ret = link->ops->detach(link);
	else
		ret = -EOPNOTSUPP;

	bpf_link_put_direct(link);
	return ret;
}

struct bpf_link *bpf_link_inc_not_zero(struct bpf_link *link)
{
	return atomic64_fetch_add_unless(&link->refcnt, 1, 0) ? link : ERR_PTR(-ENOENT);
}
EXPORT_SYMBOL(bpf_link_inc_not_zero);

struct bpf_link *bpf_link_by_id(u32 id)
{
	struct bpf_link *link;

	if (!id)
		return ERR_PTR(-ENOENT);

	spin_lock_bh(&link_idr_lock);
	/* before link is "settled", ID is 0, pretend it doesn't exist yet */
	link = idr_find(&link_idr, id);
	if (link) {
		if (link->id)
			link = bpf_link_inc_not_zero(link);
		else
			link = ERR_PTR(-EAGAIN);
	} else {
		link = ERR_PTR(-ENOENT);
	}
	spin_unlock_bh(&link_idr_lock);
	return link;
}

struct bpf_link *bpf_link_get_curr_or_next(u32 *id)
{
	struct bpf_link *link;

	spin_lock_bh(&link_idr_lock);
again:
	link = idr_get_next(&link_idr, id);
	if (link) {
		link = bpf_link_inc_not_zero(link);
		if (IS_ERR(link)) {
			(*id)++;
			goto again;
		}
	}
	spin_unlock_bh(&link_idr_lock);

	return link;
}

#define BPF_LINK_GET_FD_BY_ID_LAST_FIELD link_id

static int bpf_link_get_fd_by_id(const union bpf_attr *attr)
{
	struct bpf_link *link;
	u32 id = attr->link_id;
	int fd;

	if (CHECK_ATTR(BPF_LINK_GET_FD_BY_ID))
		return -EINVAL;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	link = bpf_link_by_id(id);
	if (IS_ERR(link))
		return PTR_ERR(link);

	fd = bpf_link_new_fd(link);
	if (fd < 0)
		bpf_link_put_direct(link);

	return fd;
}

DEFINE_MUTEX(bpf_stats_enabled_mutex);

static int bpf_stats_release(struct inode *inode, struct file *file)
{
	mutex_lock(&bpf_stats_enabled_mutex);
	static_key_slow_dec(&bpf_stats_enabled_key.key);
	mutex_unlock(&bpf_stats_enabled_mutex);
	return 0;
}

static const struct file_operations bpf_stats_fops = {
	.release = bpf_stats_release,
};

static int bpf_enable_runtime_stats(void)
{
	int fd;

	mutex_lock(&bpf_stats_enabled_mutex);

	/* Set a very high limit to avoid overflow */
	if (static_key_count(&bpf_stats_enabled_key.key) > INT_MAX / 2) {
		mutex_unlock(&bpf_stats_enabled_mutex);
		return -EBUSY;
	}

	fd = anon_inode_getfd("bpf-stats", &bpf_stats_fops, NULL, O_CLOEXEC);
	if (fd >= 0)
		static_key_slow_inc(&bpf_stats_enabled_key.key);

	mutex_unlock(&bpf_stats_enabled_mutex);
	return fd;
}

#define BPF_ENABLE_STATS_LAST_FIELD enable_stats.type

static int bpf_enable_stats(union bpf_attr *attr)
{

	if (CHECK_ATTR(BPF_ENABLE_STATS))
		return -EINVAL;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	switch (attr->enable_stats.type) {
	case BPF_STATS_RUN_TIME:
		return bpf_enable_runtime_stats();
	default:
		break;
	}
	return -EINVAL;
}

#define BPF_ITER_CREATE_LAST_FIELD iter_create.flags

static int bpf_iter_create(union bpf_attr *attr)
{
	struct bpf_link *link;
	int err;

	if (CHECK_ATTR(BPF_ITER_CREATE))
		return -EINVAL;

	if (attr->iter_create.flags)
		return -EINVAL;

	link = bpf_link_get_from_fd(attr->iter_create.link_fd);
	if (IS_ERR(link))
		return PTR_ERR(link);

	err = bpf_iter_new_fd(link);
	bpf_link_put_direct(link);

	return err;
}

#define BPF_PROG_BIND_MAP_LAST_FIELD prog_bind_map.flags

static int bpf_prog_bind_map(union bpf_attr *attr)
{
	struct bpf_prog *prog;
	struct bpf_map *map;
	struct bpf_map **used_maps_old, **used_maps_new;
	int i, ret = 0;

	if (CHECK_ATTR(BPF_PROG_BIND_MAP))
		return -EINVAL;

	if (attr->prog_bind_map.flags)
		return -EINVAL;

	prog = bpf_prog_get(attr->prog_bind_map.prog_fd);
	if (IS_ERR(prog))
		return PTR_ERR(prog);

	map = bpf_map_get(attr->prog_bind_map.map_fd);
	if (IS_ERR(map)) {
		ret = PTR_ERR(map);
		goto out_prog_put;
	}

	mutex_lock(&prog->aux->used_maps_mutex);

	used_maps_old = prog->aux->used_maps;

	for (i = 0; i < prog->aux->used_map_cnt; i++)
		if (used_maps_old[i] == map) {
			bpf_map_put(map);
			goto out_unlock;
		}

	used_maps_new = kmalloc_array(prog->aux->used_map_cnt + 1,
				      sizeof(used_maps_new[0]),
				      GFP_KERNEL);
	if (!used_maps_new) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	/* The bpf program will not access the bpf map, but for the sake of
	 * simplicity, increase sleepable_refcnt for sleepable program as well.
	 */
	if (prog->sleepable)
		atomic64_inc(&map->sleepable_refcnt);
	memcpy(used_maps_new, used_maps_old,
	       sizeof(used_maps_old[0]) * prog->aux->used_map_cnt);
	used_maps_new[prog->aux->used_map_cnt] = map;

	prog->aux->used_map_cnt++;
	prog->aux->used_maps = used_maps_new;

	kfree(used_maps_old);

out_unlock:
	mutex_unlock(&prog->aux->used_maps_mutex);

	if (ret)
		bpf_map_put(map);
out_prog_put:
	bpf_prog_put(prog);
	return ret;
}

#define BPF_TOKEN_CREATE_LAST_FIELD token_create.bpffs_fd

static int token_create(union bpf_attr *attr)
{
	if (CHECK_ATTR(BPF_TOKEN_CREATE))
		return -EINVAL;

	/* no flags are supported yet */
	if (attr->token_create.flags)
		return -EINVAL;

	return bpf_token_create(attr);
}

static int __sys_bpf(enum bpf_cmd cmd, bpfptr_t uattr, unsigned int size)
{
	union bpf_attr attr;
	int err;

	err = bpf_check_uarg_tail_zero(uattr, sizeof(attr), size);
	if (err)
		return err;
	size = min_t(u32, size, sizeof(attr));

	/* copy attributes from user space, may be less than sizeof(bpf_attr) */
	memset(&attr, 0, sizeof(attr));
	if (copy_from_bpfptr(&attr, uattr, size) != 0)
		return -EFAULT;

	err = security_bpf(cmd, &attr, size, uattr.is_kernel);
	if (err < 0)
		return err;

	switch (cmd) {
	case BPF_MAP_CREATE:
		err = map_create(&attr, uattr.is_kernel);
		break;
	case BPF_MAP_LOOKUP_ELEM:
		err = map_lookup_elem(&attr);
		break;
	case BPF_MAP_UPDATE_ELEM:
		err = map_update_elem(&attr, uattr);
		break;
	case BPF_MAP_DELETE_ELEM:
		err = map_delete_elem(&attr, uattr);
		break;
	case BPF_MAP_GET_NEXT_KEY:
		err = map_get_next_key(&attr);
		break;
	case BPF_MAP_FREEZE:
		err = map_freeze(&attr);
		break;
	case BPF_PROG_LOAD:
		err = bpf_prog_load(&attr, uattr, size);
		break;
	case BPF_OBJ_PIN:
		err = bpf_obj_pin(&attr);
		break;
	case BPF_OBJ_GET:
		err = bpf_obj_get(&attr);
		break;
	case BPF_PROG_ATTACH:
		err = bpf_prog_attach(&attr);
		break;
	case BPF_PROG_DETACH:
		err = bpf_prog_detach(&attr);
		break;
	case BPF_PROG_QUERY:
		err = bpf_prog_query(&attr, uattr.user);
		break;
	case BPF_PROG_TEST_RUN:
		err = bpf_prog_test_run(&attr, uattr.user);
		break;
	case BPF_PROG_GET_NEXT_ID:
		err = bpf_obj_get_next_id(&attr, uattr.user,
					  &prog_idr, &prog_idr_lock);
		break;
	case BPF_MAP_GET_NEXT_ID:
		err = bpf_obj_get_next_id(&attr, uattr.user,
					  &map_idr, &map_idr_lock);
		break;
	case BPF_BTF_GET_NEXT_ID:
		err = bpf_obj_get_next_id(&attr, uattr.user,
					  &btf_idr, &btf_idr_lock);
		break;
	case BPF_PROG_GET_FD_BY_ID:
		err = bpf_prog_get_fd_by_id(&attr);
		break;
	case BPF_MAP_GET_FD_BY_ID:
		err = bpf_map_get_fd_by_id(&attr);
		break;
	case BPF_OBJ_GET_INFO_BY_FD:
		err = bpf_obj_get_info_by_fd(&attr, uattr.user);
		break;
	case BPF_RAW_TRACEPOINT_OPEN:
		err = bpf_raw_tracepoint_open(&attr);
		break;
	case BPF_BTF_LOAD:
		err = bpf_btf_load(&attr, uattr, size);
		break;
	case BPF_BTF_GET_FD_BY_ID:
		err = bpf_btf_get_fd_by_id(&attr);
		break;
	case BPF_TASK_FD_QUERY:
		err = bpf_task_fd_query(&attr, uattr.user);
		break;
	case BPF_MAP_LOOKUP_AND_DELETE_ELEM:
		err = map_lookup_and_delete_elem(&attr);
		break;
	case BPF_MAP_LOOKUP_BATCH:
		err = bpf_map_do_batch(&attr, uattr.user, BPF_MAP_LOOKUP_BATCH);
		break;
	case BPF_MAP_LOOKUP_AND_DELETE_BATCH:
		err = bpf_map_do_batch(&attr, uattr.user,
				       BPF_MAP_LOOKUP_AND_DELETE_BATCH);
		break;
	case BPF_MAP_UPDATE_BATCH:
		err = bpf_map_do_batch(&attr, uattr.user, BPF_MAP_UPDATE_BATCH);
		break;
	case BPF_MAP_DELETE_BATCH:
		err = bpf_map_do_batch(&attr, uattr.user, BPF_MAP_DELETE_BATCH);
		break;
	case BPF_LINK_CREATE:
		err = link_create(&attr, uattr);
		break;
	case BPF_LINK_UPDATE:
		err = link_update(&attr);
		break;
	case BPF_LINK_GET_FD_BY_ID:
		err = bpf_link_get_fd_by_id(&attr);
		break;
	case BPF_LINK_GET_NEXT_ID:
		err = bpf_obj_get_next_id(&attr, uattr.user,
					  &link_idr, &link_idr_lock);
		break;
	case BPF_ENABLE_STATS:
		err = bpf_enable_stats(&attr);
		break;
	case BPF_ITER_CREATE:
		err = bpf_iter_create(&attr);
		break;
	case BPF_LINK_DETACH:
		err = link_detach(&attr);
		break;
	case BPF_PROG_BIND_MAP:
		err = bpf_prog_bind_map(&attr);
		break;
	case BPF_TOKEN_CREATE:
		err = token_create(&attr);
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err;
}

SYSCALL_DEFINE3(bpf, int, cmd, union bpf_attr __user *, uattr, unsigned int, size)
{
	return __sys_bpf(cmd, USER_BPFPTR(uattr), size);
}

static bool syscall_prog_is_valid_access(int off, int size,
					 enum bpf_access_type type,
					 const struct bpf_prog *prog,
					 struct bpf_insn_access_aux *info)
{
	if (off < 0 || off >= U16_MAX)
		return false;
	if (off % size != 0)
		return false;
	return true;
}

BPF_CALL_3(bpf_sys_bpf, int, cmd, union bpf_attr *, attr, u32, attr_size)
{
	switch (cmd) {
	case BPF_MAP_CREATE:
	case BPF_MAP_DELETE_ELEM:
	case BPF_MAP_UPDATE_ELEM:
	case BPF_MAP_FREEZE:
	case BPF_MAP_GET_FD_BY_ID:
	case BPF_PROG_LOAD:
	case BPF_BTF_LOAD:
	case BPF_LINK_CREATE:
	case BPF_RAW_TRACEPOINT_OPEN:
		break;
	default:
		return -EINVAL;
	}
	return __sys_bpf(cmd, KERNEL_BPFPTR(attr), attr_size);
}


/* To shut up -Wmissing-prototypes.
 * This function is used by the kernel light skeleton
 * to load bpf programs when modules are loaded or during kernel boot.
 * See tools/lib/bpf/skel_internal.h
 */
int kern_sys_bpf(int cmd, union bpf_attr *attr, unsigned int size);

int kern_sys_bpf(int cmd, union bpf_attr *attr, unsigned int size)
{
	struct bpf_prog * __maybe_unused prog;
	struct bpf_tramp_run_ctx __maybe_unused run_ctx;

	switch (cmd) {
#ifdef CONFIG_BPF_JIT /* __bpf_prog_enter_sleepable used by trampoline and JIT */
	case BPF_PROG_TEST_RUN:
		if (attr->test.data_in || attr->test.data_out ||
		    attr->test.ctx_out || attr->test.duration ||
		    attr->test.repeat || attr->test.flags)
			return -EINVAL;

		prog = bpf_prog_get_type(attr->test.prog_fd, BPF_PROG_TYPE_SYSCALL);
		if (IS_ERR(prog))
			return PTR_ERR(prog);

		if (attr->test.ctx_size_in < prog->aux->max_ctx_offset ||
		    attr->test.ctx_size_in > U16_MAX) {
			bpf_prog_put(prog);
			return -EINVAL;
		}

		run_ctx.bpf_cookie = 0;
		if (!__bpf_prog_enter_sleepable_recur(prog, &run_ctx)) {
			/* recursion detected */
			__bpf_prog_exit_sleepable_recur(prog, 0, &run_ctx);
			bpf_prog_put(prog);
			return -EBUSY;
		}
		attr->test.retval = bpf_prog_run(prog, (void *) (long) attr->test.ctx_in);
		__bpf_prog_exit_sleepable_recur(prog, 0 /* bpf_prog_run does runtime stats */,
						&run_ctx);
		bpf_prog_put(prog);
		return 0;
#endif
	default:
		return ____bpf_sys_bpf(cmd, attr, size);
	}
}
EXPORT_SYMBOL_NS(kern_sys_bpf, "BPF_INTERNAL");

static const struct bpf_func_proto bpf_sys_bpf_proto = {
	.func		= bpf_sys_bpf,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_ANYTHING,
	.arg2_type	= ARG_PTR_TO_MEM | MEM_RDONLY,
	.arg3_type	= ARG_CONST_SIZE,
};

const struct bpf_func_proto * __weak
tracing_prog_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	return bpf_base_func_proto(func_id, prog);
}

BPF_CALL_1(bpf_sys_close, u32, fd)
{
	/* When bpf program calls this helper there should not be
	 * an fdget() without matching completed fdput().
	 * This helper is allowed in the following callchain only:
	 * sys_bpf->prog_test_run->bpf_prog->bpf_sys_close
	 */
	return close_fd(fd);
}

static const struct bpf_func_proto bpf_sys_close_proto = {
	.func		= bpf_sys_close,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_ANYTHING,
};

BPF_CALL_4(bpf_kallsyms_lookup_name, const char *, name, int, name_sz, int, flags, u64 *, res)
{
	*res = 0;
	if (flags)
		return -EINVAL;

	if (name_sz <= 1 || name[name_sz - 1])
		return -EINVAL;

	if (!bpf_dump_raw_ok(current_cred()))
		return -EPERM;

	*res = kallsyms_lookup_name(name);
	return *res ? 0 : -ENOENT;
}

static const struct bpf_func_proto bpf_kallsyms_lookup_name_proto = {
	.func		= bpf_kallsyms_lookup_name,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_MEM,
	.arg2_type	= ARG_CONST_SIZE_OR_ZERO,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_PTR_TO_FIXED_SIZE_MEM | MEM_UNINIT | MEM_WRITE | MEM_ALIGNED,
	.arg4_size	= sizeof(u64),
};

static const struct bpf_func_proto *
syscall_prog_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_sys_bpf:
		return !bpf_token_capable(prog->aux->token, CAP_PERFMON)
		       ? NULL : &bpf_sys_bpf_proto;
	case BPF_FUNC_btf_find_by_name_kind:
		return &bpf_btf_find_by_name_kind_proto;
	case BPF_FUNC_sys_close:
		return &bpf_sys_close_proto;
	case BPF_FUNC_kallsyms_lookup_name:
		return &bpf_kallsyms_lookup_name_proto;
	default:
		return tracing_prog_func_proto(func_id, prog);
	}
}

const struct bpf_verifier_ops bpf_syscall_verifier_ops = {
	.get_func_proto  = syscall_prog_func_proto,
	.is_valid_access = syscall_prog_is_valid_access,
};

const struct bpf_prog_ops bpf_syscall_prog_ops = {
	.test_run = bpf_prog_test_run_syscall,
};

#ifdef CONFIG_SYSCTL
static int bpf_stats_handler(const struct ctl_table *table, int write,
			     void *buffer, size_t *lenp, loff_t *ppos)
{
	struct static_key *key = (struct static_key *)table->data;
	static int saved_val;
	int val, ret;
	struct ctl_table tmp = {
		.data   = &val,
		.maxlen = sizeof(val),
		.mode   = table->mode,
		.extra1 = SYSCTL_ZERO,
		.extra2 = SYSCTL_ONE,
	};

	if (write && !capable(CAP_SYS_ADMIN))
		return -EPERM;

	mutex_lock(&bpf_stats_enabled_mutex);
	val = saved_val;
	ret = proc_dointvec_minmax(&tmp, write, buffer, lenp, ppos);
	if (write && !ret && val != saved_val) {
		if (val)
			static_key_slow_inc(key);
		else
			static_key_slow_dec(key);
		saved_val = val;
	}
	mutex_unlock(&bpf_stats_enabled_mutex);
	return ret;
}

void __weak unpriv_ebpf_notify(int new_state)
{
}

static int bpf_unpriv_handler(const struct ctl_table *table, int write,
			      void *buffer, size_t *lenp, loff_t *ppos)
{
	int ret, unpriv_enable = *(int *)table->data;
	bool locked_state = unpriv_enable == 1;
	struct ctl_table tmp = *table;

	if (write && !capable(CAP_SYS_ADMIN))
		return -EPERM;

	tmp.data = &unpriv_enable;
	ret = proc_dointvec_minmax(&tmp, write, buffer, lenp, ppos);
	if (write && !ret) {
		if (locked_state && unpriv_enable != 1)
			return -EPERM;
		*(int *)table->data = unpriv_enable;
	}

	if (write)
		unpriv_ebpf_notify(unpriv_enable);

	return ret;
}

static const struct ctl_table bpf_syscall_table[] = {
	{
		.procname	= "unprivileged_bpf_disabled",
		.data		= &sysctl_unprivileged_bpf_disabled,
		.maxlen		= sizeof(sysctl_unprivileged_bpf_disabled),
		.mode		= 0644,
		.proc_handler	= bpf_unpriv_handler,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_TWO,
	},
	{
		.procname	= "bpf_stats_enabled",
		.data		= &bpf_stats_enabled_key.key,
		.mode		= 0644,
		.proc_handler	= bpf_stats_handler,
	},
};

static int __init bpf_syscall_sysctl_init(void)
{
	register_sysctl_init("kernel", bpf_syscall_table);
	return 0;
}
late_initcall(bpf_syscall_sysctl_init);
#endif /* CONFIG_SYSCTL */
