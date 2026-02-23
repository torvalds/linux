/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Arena-based task data scheduler. This is a variation of scx_simple
 * that uses a combined allocator and indexing structure to organize
 * task data. Task context allocation is done when a task enters the
 * scheduler, while freeing is done when it exits. Task contexts are
 * retrieved from task-local storage, pointing to the allocated memory.
 *
 * The main purpose of this scheduler is to demostrate arena memory
 * management.
 *
 * Copyright (c) 2024-2025 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2024-2025 Emil Tsalapatis <etsal@meta.com>
 * Copyright (c) 2024-2025 Tejun Heo <tj@kernel.org>
 *
 */
#include <scx/common.bpf.h>
#include <scx/bpf_arena_common.bpf.h>

#include "scx_sdt.h"

char _license[] SEC("license") = "GPL";

UEI_DEFINE(uei);

struct {
	__uint(type, BPF_MAP_TYPE_ARENA);
	__uint(map_flags, BPF_F_MMAPABLE);
#if defined(__TARGET_ARCH_arm64) || defined(__aarch64__)
	__uint(max_entries, 1 << 16); /* number of pages */
        __ulong(map_extra, (1ull << 32)); /* start of mmap() region */
#else
	__uint(max_entries, 1 << 20); /* number of pages */
        __ulong(map_extra, (1ull << 44)); /* start of mmap() region */
#endif
} arena __weak SEC(".maps");

#define SHARED_DSQ 0

#define DEFINE_SDT_STAT(metric)				\
static inline void				\
stat_inc_##metric(struct scx_stats __arena *stats)	\
{							\
	cast_kern(stats);				\
	stats->metric += 1;				\
}							\
__u64 stat_##metric;					\

DEFINE_SDT_STAT(enqueue);
DEFINE_SDT_STAT(init);
DEFINE_SDT_STAT(exit);
DEFINE_SDT_STAT(select_idle_cpu);
DEFINE_SDT_STAT(select_busy_cpu);

/*
 * Necessary for cond_break/can_loop's semantics. According to kernel commit
 * 011832b, the loop counter variable must be seen as imprecise and bounded
 * by the verifier. Initializing it from a constant (e.g., i = 0;), then,
 * makes it precise and prevents may_goto from helping with converging the
 * loop. For these loops we must initialize the loop counter from a variable
 * whose value the verifier cannot reason about when checking the program, so
 * that the loop counter's value is imprecise.
 */
static __u64 zero = 0;

/*
 * XXX Hack to get the verifier to find the arena for sdt_exit_task.
 * As of 6.12-rc5, The verifier associates arenas with programs by
 * checking LD.IMM instruction operands for an arena and populating
 * the program state with the first instance it finds. This requires
 * accessing our global arena variable, but scx methods do not necessarily
 * do so while still using pointers from that arena. Insert a bpf_printk
 * statement that triggers at most once to generate an LD.IMM instruction
 * to access the arena and help the verifier.
 */
static volatile bool scx_arena_verify_once;

__hidden void scx_arena_subprog_init(void)
{
	if (scx_arena_verify_once)
		return;

	bpf_printk("%s: arena pointer %p", __func__, &arena);
	scx_arena_verify_once = true;
}


private(LOCK) struct bpf_spin_lock alloc_lock;
private(POOL_LOCK) struct bpf_spin_lock alloc_pool_lock;

/* allocation pools */
struct sdt_pool desc_pool;
struct sdt_pool chunk_pool;

/* Protected by alloc_lock. */
struct scx_alloc_stats alloc_stats;


/* Allocate element from the pool. Must be called with a then pool lock held. */
static
void __arena *scx_alloc_from_pool(struct sdt_pool *pool)
{
	__u64 elem_size, max_elems;
	void __arena *slab;
	void __arena *ptr;

	elem_size = pool->elem_size;
	max_elems = pool->max_elems;

	/* If the chunk is spent, get a new one. */
	if (pool->idx >= max_elems) {
		slab = bpf_arena_alloc_pages(&arena, NULL,
			div_round_up(max_elems * elem_size, PAGE_SIZE), NUMA_NO_NODE, 0);
		if (!slab)
			return NULL;

		pool->slab = slab;
		pool->idx = 0;
	}

	ptr = (void __arena *)((__u64) pool->slab + elem_size * pool->idx);
	pool->idx += 1;

	return ptr;
}

/* Alloc desc and associated chunk. Called with the allocator spinlock held. */
static sdt_desc_t *scx_alloc_chunk(void)
{
	struct sdt_chunk __arena *chunk;
	sdt_desc_t *desc;
	sdt_desc_t *out;

	chunk = scx_alloc_from_pool(&chunk_pool);
	if (!chunk)
		return NULL;

	desc = scx_alloc_from_pool(&desc_pool);
	if (!desc) {
		/*
		 * Effectively frees the previous chunk allocation.
		 * Index cannot be 0, so decrementing is always
		 * valid.
		 */
		chunk_pool.idx -= 1;
		return NULL;
	}

	out = desc;

	desc->nr_free = SDT_TASK_ENTS_PER_CHUNK;
	desc->chunk = chunk;

	alloc_stats.chunk_allocs += 1;

	return out;
}

static int pool_set_size(struct sdt_pool *pool, __u64 data_size, __u64 nr_pages)
{
	if (unlikely(data_size % 8))
		return -EINVAL;

	if (unlikely(nr_pages == 0))
		return -EINVAL;

	pool->elem_size = data_size;
	pool->max_elems = (PAGE_SIZE * nr_pages) / pool->elem_size;
	/* Populate the pool slab on the first allocation. */
	pool->idx = pool->max_elems;

	return 0;
}

/* Initialize both the base pool allocators and the root chunk of the index. */
__hidden int
scx_alloc_init(struct scx_allocator *alloc, __u64 data_size)
{
	size_t min_chunk_size;
	int ret;

	_Static_assert(sizeof(struct sdt_chunk) <= PAGE_SIZE,
		"chunk size must fit into a page");

	ret = pool_set_size(&chunk_pool, sizeof(struct sdt_chunk), 1);
	if (ret != 0)
		return ret;

	ret = pool_set_size(&desc_pool, sizeof(struct sdt_desc), 1);
	if (ret != 0)
		return ret;

	/* Wrap data into a descriptor and word align. */
	data_size += sizeof(struct sdt_data);
	data_size = round_up(data_size, 8);

	/*
	 * Ensure we allocate large enough chunks from the arena to avoid excessive
	 * internal fragmentation when turning chunks it into structs.
	 */
	min_chunk_size = div_round_up(SDT_TASK_MIN_ELEM_PER_ALLOC * data_size, PAGE_SIZE);
	ret = pool_set_size(&alloc->pool, data_size, min_chunk_size);
	if (ret != 0)
		return ret;

	bpf_spin_lock(&alloc_lock);
	alloc->root = scx_alloc_chunk();
	bpf_spin_unlock(&alloc_lock);
	if (!alloc->root)
		return -ENOMEM;

	return 0;
}

static
int set_idx_state(sdt_desc_t *desc, __u64 pos, bool state)
{
	__u64 __arena *allocated = desc->allocated;
	__u64 bit;

	if (unlikely(pos >= SDT_TASK_ENTS_PER_CHUNK))
		return -EINVAL;

	bit = (__u64)1 << (pos % 64);

	if (state)
		allocated[pos / 64] |= bit;
	else
		allocated[pos / 64] &= ~bit;

	return 0;
}

static __noinline
int mark_nodes_avail(sdt_desc_t *lv_desc[SDT_TASK_LEVELS], __u64 lv_pos[SDT_TASK_LEVELS])
{
	sdt_desc_t *desc;
	__u64 u, level;
	int ret;

	for (u = zero; u < SDT_TASK_LEVELS && can_loop; u++) {
		level = SDT_TASK_LEVELS - 1 - u;

		/* Only propagate upwards if we are the parent's only free chunk. */
		desc = lv_desc[level];

		ret = set_idx_state(desc, lv_pos[level], false);
		if (unlikely(ret != 0))
			return ret;

		desc->nr_free += 1;
		if (desc->nr_free > 1)
			return 0;
	}

	return 0;
}

/*
 * Free the allocated struct with the given index. Called with the
 * allocator lock taken.
 */
__hidden
int scx_alloc_free_idx(struct scx_allocator *alloc, __u64 idx)
{
	const __u64 mask = (1 << SDT_TASK_ENTS_PER_PAGE_SHIFT) - 1;
	sdt_desc_t *lv_desc[SDT_TASK_LEVELS];
	sdt_desc_t * __arena *desc_children;
	struct sdt_chunk __arena *chunk;
	sdt_desc_t *desc;
	struct sdt_data __arena *data;
	__u64 level, shift, pos;
	__u64 lv_pos[SDT_TASK_LEVELS];
	int ret;
	int i;

	if (!alloc)
		return 0;

	desc = alloc->root;
	if (unlikely(!desc))
		return -EINVAL;

	/* To appease the verifier. */
	for (level = zero; level < SDT_TASK_LEVELS && can_loop; level++) {
		lv_desc[level] = NULL;
		lv_pos[level] = 0;
	}

	/* Find the leaf node containing the index. */
	for (level = zero; level < SDT_TASK_LEVELS && can_loop; level++) {
		shift = (SDT_TASK_LEVELS - 1 - level) * SDT_TASK_ENTS_PER_PAGE_SHIFT;
		pos = (idx >> shift) & mask;

		lv_desc[level] = desc;
		lv_pos[level] = pos;

		if (level == SDT_TASK_LEVELS - 1)
			break;

		chunk = desc->chunk;

		desc_children = (sdt_desc_t * __arena *)chunk->descs;
		desc = desc_children[pos];

		if (unlikely(!desc))
			return -EINVAL;
	}

	chunk = desc->chunk;

	pos = idx & mask;
	data = chunk->data[pos];
	if (likely(data)) {
		*data = (struct sdt_data) {
			.tid.genn = data->tid.genn + 1,
		};

		/* Zero out one word at a time. */
		for (i = zero; i < alloc->pool.elem_size / 8 && can_loop; i++) {
			data->payload[i] = 0;
		}
	}

	ret = mark_nodes_avail(lv_desc, lv_pos);
	if (unlikely(ret != 0))
		return ret;

	alloc_stats.active_allocs -= 1;
	alloc_stats.free_ops += 1;

	return 0;
}

static inline
int ffs(__u64 word)
{
	unsigned int num = 0;

	if ((word & 0xffffffff) == 0) {
		num += 32;
		word >>= 32;
	}

	if ((word & 0xffff) == 0) {
		num += 16;
		word >>= 16;
	}

	if ((word & 0xff) == 0) {
		num += 8;
		word >>= 8;
	}

	if ((word & 0xf) == 0) {
		num += 4;
		word >>= 4;
	}

	if ((word & 0x3) == 0) {
		num += 2;
		word >>= 2;
	}

	if ((word & 0x1) == 0) {
		num += 1;
		word >>= 1;
	}

	return num;
}


/* find the first empty slot */
__hidden
__u64 chunk_find_empty(sdt_desc_t __arg_arena *desc)
{
	__u64 freeslots;
	__u64 i;

	for (i = 0; i < SDT_TASK_CHUNK_BITMAP_U64S; i++) {
		freeslots = ~desc->allocated[i];
		if (freeslots == (__u64)0)
			continue;

		return (i * 64) + ffs(freeslots);
	}

	return SDT_TASK_ENTS_PER_CHUNK;
}

/*
 * Find and return an available idx on the allocator.
 * Called with the task spinlock held.
 */
static sdt_desc_t * desc_find_empty(sdt_desc_t *desc, __u64 *idxp)
{
	sdt_desc_t *lv_desc[SDT_TASK_LEVELS];
	sdt_desc_t * __arena *desc_children;
	struct sdt_chunk __arena *chunk;
	sdt_desc_t *tmp;
	__u64 lv_pos[SDT_TASK_LEVELS];
	__u64 u, pos, level;
	__u64 idx = 0;
	int ret;

	for (level = zero; level < SDT_TASK_LEVELS && can_loop; level++) {
		pos = chunk_find_empty(desc);

		/* If we error out, something has gone very wrong. */
		if (unlikely(pos > SDT_TASK_ENTS_PER_CHUNK))
			return NULL;

		if (pos == SDT_TASK_ENTS_PER_CHUNK)
			return NULL;

		idx <<= SDT_TASK_ENTS_PER_PAGE_SHIFT;
		idx |= pos;

		/* Log the levels to complete allocation. */
		lv_desc[level] = desc;
		lv_pos[level] = pos;

		/* The rest of the loop is for internal node traversal. */
		if (level == SDT_TASK_LEVELS - 1)
			break;

		/* Allocate an internal node if necessary. */
		chunk = desc->chunk;
		desc_children = (sdt_desc_t * __arena *)chunk->descs;

		desc = desc_children[pos];
		if (!desc) {
			desc = scx_alloc_chunk();
			if (!desc)
				return NULL;

			desc_children[pos] = desc;
		}
	}

	/*
	 * Finding the descriptor along with any internal node
	 * allocations was successful. Update all levels with
	 * the new allocation.
	 */
	bpf_for(u, 0, SDT_TASK_LEVELS) {
		level = SDT_TASK_LEVELS - 1 - u;
		tmp = lv_desc[level];

		ret = set_idx_state(tmp, lv_pos[level], true);
		if (ret != 0)
			break;

		tmp->nr_free -= 1;
		if (tmp->nr_free > 0)
			break;
	}

	*idxp = idx;

	return desc;
}

__hidden
void __arena *scx_alloc(struct scx_allocator *alloc)
{
	struct sdt_data __arena *data = NULL;
	struct sdt_chunk __arena *chunk;
	sdt_desc_t *desc;
	__u64 idx, pos;

	if (!alloc)
		return NULL;

	bpf_spin_lock(&alloc_lock);

	/* We unlock if we encounter an error in the function. */
	desc = desc_find_empty(alloc->root, &idx);
	if (unlikely(desc == NULL)) {
		bpf_spin_unlock(&alloc_lock);
		return NULL;
	}

	chunk = desc->chunk;

	/* Populate the leaf node if necessary. */
	pos = idx & (SDT_TASK_ENTS_PER_CHUNK - 1);
	data = chunk->data[pos];
	if (!data) {
		data = scx_alloc_from_pool(&alloc->pool);
		if (!data) {
			scx_alloc_free_idx(alloc, idx);
			bpf_spin_unlock(&alloc_lock);
			return NULL;
		}
	}

	chunk->data[pos] = data;

	/* The data counts as a chunk */
	alloc_stats.data_allocs += 1;
	alloc_stats.alloc_ops += 1;
	alloc_stats.active_allocs += 1;

	data->tid.idx = idx;

	bpf_spin_unlock(&alloc_lock);

	return data;
}

/*
 * Task BPF map entry recording the task's assigned ID and pointing to the data
 * area allocated in arena.
 */
struct scx_task_map_val {
	union sdt_id		tid;
	__u64			tptr;
	struct sdt_data __arena	*data;
};

struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct scx_task_map_val);
} scx_task_map SEC(".maps");

static struct scx_allocator scx_task_allocator;

__hidden
void __arena *scx_task_alloc(struct task_struct *p)
{
	struct sdt_data __arena *data = NULL;
	struct scx_task_map_val *mval;

	mval = bpf_task_storage_get(&scx_task_map, p, 0,
				    BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (!mval)
		return NULL;

	data = scx_alloc(&scx_task_allocator);
	if (unlikely(!data))
		return NULL;

	mval->tid = data->tid;
	mval->tptr = (__u64) p;
	mval->data = data;

	return (void __arena *)data->payload;
}

__hidden
int scx_task_init(__u64 data_size)
{
	return scx_alloc_init(&scx_task_allocator, data_size);
}

__hidden
void __arena *scx_task_data(struct task_struct *p)
{
	struct sdt_data __arena *data;
	struct scx_task_map_val *mval;

	scx_arena_subprog_init();

	mval = bpf_task_storage_get(&scx_task_map, p, 0, 0);
	if (!mval)
		return NULL;

	data = mval->data;

	return (void __arena *)data->payload;
}

__hidden
void scx_task_free(struct task_struct *p)
{
	struct scx_task_map_val *mval;

	scx_arena_subprog_init();

	mval = bpf_task_storage_get(&scx_task_map, p, 0, 0);
	if (!mval)
		return;

	bpf_spin_lock(&alloc_lock);
	scx_alloc_free_idx(&scx_task_allocator, mval->tid.idx);
	bpf_spin_unlock(&alloc_lock);

	bpf_task_storage_delete(&scx_task_map, p);
}

static inline void
scx_stat_global_update(struct scx_stats __arena *stats)
{
	cast_kern(stats);
	__sync_fetch_and_add(&stat_enqueue, stats->enqueue);
	__sync_fetch_and_add(&stat_init, stats->init);
	__sync_fetch_and_add(&stat_exit, stats->exit);
	__sync_fetch_and_add(&stat_select_idle_cpu, stats->select_idle_cpu);
	__sync_fetch_and_add(&stat_select_busy_cpu, stats->select_busy_cpu);
}

s32 BPF_STRUCT_OPS(sdt_select_cpu, struct task_struct *p, s32 prev_cpu, u64 wake_flags)
{
	struct scx_stats __arena *stats;
	bool is_idle = false;
	s32 cpu;

	stats = scx_task_data(p);
	if (!stats) {
		scx_bpf_error("%s: no stats for pid %d", __func__, p->pid);
		return 0;
	}

	cpu = scx_bpf_select_cpu_dfl(p, prev_cpu, wake_flags, &is_idle);
	if (is_idle) {
		stat_inc_select_idle_cpu(stats);
		scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL, SCX_SLICE_DFL, 0);
	} else {
		stat_inc_select_busy_cpu(stats);
	}

	return cpu;
}

void BPF_STRUCT_OPS(sdt_enqueue, struct task_struct *p, u64 enq_flags)
{
	struct scx_stats __arena *stats;

	stats = scx_task_data(p);
	if (!stats) {
		scx_bpf_error("%s: no stats for pid %d", __func__, p->pid);
		return;
	}

	stat_inc_enqueue(stats);

	scx_bpf_dsq_insert(p, SHARED_DSQ, SCX_SLICE_DFL, enq_flags);
}

void BPF_STRUCT_OPS(sdt_dispatch, s32 cpu, struct task_struct *prev)
{
	scx_bpf_dsq_move_to_local(SHARED_DSQ);
}

s32 BPF_STRUCT_OPS_SLEEPABLE(sdt_init_task, struct task_struct *p,
			     struct scx_init_task_args *args)
{
	struct scx_stats __arena *stats;

	stats = scx_task_alloc(p);
	if (!stats) {
		scx_bpf_error("arena allocator out of memory");
		return -ENOMEM;
	}

	stats->pid = p->pid;

	stat_inc_init(stats);

	return 0;
}

void BPF_STRUCT_OPS(sdt_exit_task, struct task_struct *p,
			      struct scx_exit_task_args *args)
{
	struct scx_stats __arena *stats;

	stats = scx_task_data(p);
	if (!stats) {
		scx_bpf_error("%s: no stats for pid %d", __func__, p->pid);
		return;
	}

	stat_inc_exit(stats);
	scx_stat_global_update(stats);

	scx_task_free(p);
}

s32 BPF_STRUCT_OPS_SLEEPABLE(sdt_init)
{
	int ret;

	ret = scx_task_init(sizeof(struct scx_stats));
	if (ret < 0) {
		scx_bpf_error("%s: failed with %d", __func__, ret);
		return ret;
	}

	ret = scx_bpf_create_dsq(SHARED_DSQ, -1);
	if (ret) {
		scx_bpf_error("failed to create DSQ %d (%d)", SHARED_DSQ, ret);
		return ret;
	}

	return 0;
}

void BPF_STRUCT_OPS(sdt_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
}

SCX_OPS_DEFINE(sdt_ops,
	       .select_cpu		= (void *)sdt_select_cpu,
	       .enqueue			= (void *)sdt_enqueue,
	       .dispatch		= (void *)sdt_dispatch,
	       .init_task		= (void *)sdt_init_task,
	       .exit_task		= (void *)sdt_exit_task,
	       .init			= (void *)sdt_init,
	       .exit			= (void *)sdt_exit,
	       .name			= "sdt");
