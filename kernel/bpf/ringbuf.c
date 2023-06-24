#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/err.h>
#include <linux/irq_work.h>
#include <linux/slab.h>
#include <linux/filter.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/kmemleak.h>
#include <uapi/linux/btf.h>
#include <linux/btf_ids.h>

#define RINGBUF_CREATE_FLAG_MASK (BPF_F_NUMA_NODE)

/* non-mmap()'able part of bpf_ringbuf (everything up to consumer page) */
#define RINGBUF_PGOFF \
	(offsetof(struct bpf_ringbuf, consumer_pos) >> PAGE_SHIFT)
/* consumer page and producer page */
#define RINGBUF_POS_PAGES 2

#define RINGBUF_MAX_RECORD_SZ (UINT_MAX/4)

/* Maximum size of ring buffer area is limited by 32-bit page offset within
 * record header, counted in pages. Reserve 8 bits for extensibility, and take
 * into account few extra pages for consumer/producer pages and
 * non-mmap()'able parts. This gives 64GB limit, which seems plenty for single
 * ring buffer.
 */
#define RINGBUF_MAX_DATA_SZ \
	(((1ULL << 24) - RINGBUF_POS_PAGES - RINGBUF_PGOFF) * PAGE_SIZE)

struct bpf_ringbuf {
	wait_queue_head_t waitq;
	struct irq_work work;
	u64 mask;
	struct page **pages;
	int nr_pages;
	spinlock_t spinlock ____cacheline_aligned_in_smp;
	/* For user-space producer ring buffers, an atomic_t busy bit is used
	 * to synchronize access to the ring buffers in the kernel, rather than
	 * the spinlock that is used for kernel-producer ring buffers. This is
	 * done because the ring buffer must hold a lock across a BPF program's
	 * callback:
	 *
	 *    __bpf_user_ringbuf_peek() // lock acquired
	 * -> program callback_fn()
	 * -> __bpf_user_ringbuf_sample_release() // lock released
	 *
	 * It is unsafe and incorrect to hold an IRQ spinlock across what could
	 * be a long execution window, so we instead simply disallow concurrent
	 * access to the ring buffer by kernel consumers, and return -EBUSY from
	 * __bpf_user_ringbuf_peek() if the busy bit is held by another task.
	 */
	atomic_t busy ____cacheline_aligned_in_smp;
	/* Consumer and producer counters are put into separate pages to
	 * allow each position to be mapped with different permissions.
	 * This prevents a user-space application from modifying the
	 * position and ruining in-kernel tracking. The permissions of the
	 * pages depend on who is producing samples: user-space or the
	 * kernel.
	 *
	 * Kernel-producer
	 * ---------------
	 * The producer position and data pages are mapped as r/o in
	 * userspace. For this approach, bits in the header of samples are
	 * used to signal to user-space, and to other producers, whether a
	 * sample is currently being written.
	 *
	 * User-space producer
	 * -------------------
	 * Only the page containing the consumer position is mapped r/o in
	 * user-space. User-space producers also use bits of the header to
	 * communicate to the kernel, but the kernel must carefully check and
	 * validate each sample to ensure that they're correctly formatted, and
	 * fully contained within the ring buffer.
	 */
	unsigned long consumer_pos __aligned(PAGE_SIZE);
	unsigned long producer_pos __aligned(PAGE_SIZE);
	char data[] __aligned(PAGE_SIZE);
};

struct bpf_ringbuf_map {
	struct bpf_map map;
	struct bpf_ringbuf *rb;
};

/* 8-byte ring buffer record header structure */
struct bpf_ringbuf_hdr {
	u32 len;
	u32 pg_off;
};

static struct bpf_ringbuf *bpf_ringbuf_area_alloc(size_t data_sz, int numa_node)
{
	const gfp_t flags = GFP_KERNEL_ACCOUNT | __GFP_RETRY_MAYFAIL |
			    __GFP_NOWARN | __GFP_ZERO;
	int nr_meta_pages = RINGBUF_PGOFF + RINGBUF_POS_PAGES;
	int nr_data_pages = data_sz >> PAGE_SHIFT;
	int nr_pages = nr_meta_pages + nr_data_pages;
	struct page **pages, *page;
	struct bpf_ringbuf *rb;
	size_t array_size;
	int i;

	/* Each data page is mapped twice to allow "virtual"
	 * continuous read of samples wrapping around the end of ring
	 * buffer area:
	 * ------------------------------------------------------
	 * | meta pages |  real data pages  |  same data pages  |
	 * ------------------------------------------------------
	 * |            | 1 2 3 4 5 6 7 8 9 | 1 2 3 4 5 6 7 8 9 |
	 * ------------------------------------------------------
	 * |            | TA             DA | TA             DA |
	 * ------------------------------------------------------
	 *                               ^^^^^^^
	 *                                  |
	 * Here, no need to worry about special handling of wrapped-around
	 * data due to double-mapped data pages. This works both in kernel and
	 * when mmap()'ed in user-space, simplifying both kernel and
	 * user-space implementations significantly.
	 */
	array_size = (nr_meta_pages + 2 * nr_data_pages) * sizeof(*pages);
	pages = bpf_map_area_alloc(array_size, numa_node);
	if (!pages)
		return NULL;

	for (i = 0; i < nr_pages; i++) {
		page = alloc_pages_node(numa_node, flags, 0);
		if (!page) {
			nr_pages = i;
			goto err_free_pages;
		}
		pages[i] = page;
		if (i >= nr_meta_pages)
			pages[nr_data_pages + i] = page;
	}

	rb = vmap(pages, nr_meta_pages + 2 * nr_data_pages,
		  VM_MAP | VM_USERMAP, PAGE_KERNEL);
	if (rb) {
		kmemleak_not_leak(pages);
		rb->pages = pages;
		rb->nr_pages = nr_pages;
		return rb;
	}

err_free_pages:
	for (i = 0; i < nr_pages; i++)
		__free_page(pages[i]);
	bpf_map_area_free(pages);
	return NULL;
}

static void bpf_ringbuf_notify(struct irq_work *work)
{
	struct bpf_ringbuf *rb = container_of(work, struct bpf_ringbuf, work);

	wake_up_all(&rb->waitq);
}

static struct bpf_ringbuf *bpf_ringbuf_alloc(size_t data_sz, int numa_node)
{
	struct bpf_ringbuf *rb;

	rb = bpf_ringbuf_area_alloc(data_sz, numa_node);
	if (!rb)
		return NULL;

	spin_lock_init(&rb->spinlock);
	atomic_set(&rb->busy, 0);
	init_waitqueue_head(&rb->waitq);
	init_irq_work(&rb->work, bpf_ringbuf_notify);

	rb->mask = data_sz - 1;
	rb->consumer_pos = 0;
	rb->producer_pos = 0;

	return rb;
}

static struct bpf_map *ringbuf_map_alloc(union bpf_attr *attr)
{
	struct bpf_ringbuf_map *rb_map;

	if (attr->map_flags & ~RINGBUF_CREATE_FLAG_MASK)
		return ERR_PTR(-EINVAL);

	if (attr->key_size || attr->value_size ||
	    !is_power_of_2(attr->max_entries) ||
	    !PAGE_ALIGNED(attr->max_entries))
		return ERR_PTR(-EINVAL);

#ifdef CONFIG_64BIT
	/* on 32-bit arch, it's impossible to overflow record's hdr->pgoff */
	if (attr->max_entries > RINGBUF_MAX_DATA_SZ)
		return ERR_PTR(-E2BIG);
#endif

	rb_map = bpf_map_area_alloc(sizeof(*rb_map), NUMA_NO_NODE);
	if (!rb_map)
		return ERR_PTR(-ENOMEM);

	bpf_map_init_from_attr(&rb_map->map, attr);

	rb_map->rb = bpf_ringbuf_alloc(attr->max_entries, rb_map->map.numa_node);
	if (!rb_map->rb) {
		bpf_map_area_free(rb_map);
		return ERR_PTR(-ENOMEM);
	}

	return &rb_map->map;
}

static void bpf_ringbuf_free(struct bpf_ringbuf *rb)
{
	/* copy pages pointer and nr_pages to local variable, as we are going
	 * to unmap rb itself with vunmap() below
	 */
	struct page **pages = rb->pages;
	int i, nr_pages = rb->nr_pages;

	vunmap(rb);
	for (i = 0; i < nr_pages; i++)
		__free_page(pages[i]);
	bpf_map_area_free(pages);
}

static void ringbuf_map_free(struct bpf_map *map)
{
	struct bpf_ringbuf_map *rb_map;

	rb_map = container_of(map, struct bpf_ringbuf_map, map);
	bpf_ringbuf_free(rb_map->rb);
	bpf_map_area_free(rb_map);
}

static void *ringbuf_map_lookup_elem(struct bpf_map *map, void *key)
{
	return ERR_PTR(-ENOTSUPP);
}

static int ringbuf_map_update_elem(struct bpf_map *map, void *key, void *value,
				   u64 flags)
{
	return -ENOTSUPP;
}

static int ringbuf_map_delete_elem(struct bpf_map *map, void *key)
{
	return -ENOTSUPP;
}

static int ringbuf_map_get_next_key(struct bpf_map *map, void *key,
				    void *next_key)
{
	return -ENOTSUPP;
}

static int ringbuf_map_mmap_kern(struct bpf_map *map, struct vm_area_struct *vma)
{
	struct bpf_ringbuf_map *rb_map;

	rb_map = container_of(map, struct bpf_ringbuf_map, map);

	if (vma->vm_flags & VM_WRITE) {
		/* allow writable mapping for the consumer_pos only */
		if (vma->vm_pgoff != 0 || vma->vm_end - vma->vm_start != PAGE_SIZE)
			return -EPERM;
	} else {
		vma->vm_flags &= ~VM_MAYWRITE;
	}
	/* remap_vmalloc_range() checks size and offset constraints */
	return remap_vmalloc_range(vma, rb_map->rb,
				   vma->vm_pgoff + RINGBUF_PGOFF);
}

static int ringbuf_map_mmap_user(struct bpf_map *map, struct vm_area_struct *vma)
{
	struct bpf_ringbuf_map *rb_map;

	rb_map = container_of(map, struct bpf_ringbuf_map, map);

	if (vma->vm_flags & VM_WRITE) {
		if (vma->vm_pgoff == 0)
			/* Disallow writable mappings to the consumer pointer,
			 * and allow writable mappings to both the producer
			 * position, and the ring buffer data itself.
			 */
			return -EPERM;
	} else {
		vma->vm_flags &= ~VM_MAYWRITE;
	}
	/* remap_vmalloc_range() checks size and offset constraints */
	return remap_vmalloc_range(vma, rb_map->rb, vma->vm_pgoff + RINGBUF_PGOFF);
}

static unsigned long ringbuf_avail_data_sz(struct bpf_ringbuf *rb)
{
	unsigned long cons_pos, prod_pos;

	cons_pos = smp_load_acquire(&rb->consumer_pos);
	prod_pos = smp_load_acquire(&rb->producer_pos);
	return prod_pos - cons_pos;
}

static u32 ringbuf_total_data_sz(const struct bpf_ringbuf *rb)
{
	return rb->mask + 1;
}

static __poll_t ringbuf_map_poll_kern(struct bpf_map *map, struct file *filp,
				      struct poll_table_struct *pts)
{
	struct bpf_ringbuf_map *rb_map;

	rb_map = container_of(map, struct bpf_ringbuf_map, map);
	poll_wait(filp, &rb_map->rb->waitq, pts);

	if (ringbuf_avail_data_sz(rb_map->rb))
		return EPOLLIN | EPOLLRDNORM;
	return 0;
}

static __poll_t ringbuf_map_poll_user(struct bpf_map *map, struct file *filp,
				      struct poll_table_struct *pts)
{
	struct bpf_ringbuf_map *rb_map;

	rb_map = container_of(map, struct bpf_ringbuf_map, map);
	poll_wait(filp, &rb_map->rb->waitq, pts);

	if (ringbuf_avail_data_sz(rb_map->rb) < ringbuf_total_data_sz(rb_map->rb))
		return EPOLLOUT | EPOLLWRNORM;
	return 0;
}

BTF_ID_LIST_SINGLE(ringbuf_map_btf_ids, struct, bpf_ringbuf_map)
const struct bpf_map_ops ringbuf_map_ops = {
	.map_meta_equal = bpf_map_meta_equal,
	.map_alloc = ringbuf_map_alloc,
	.map_free = ringbuf_map_free,
	.map_mmap = ringbuf_map_mmap_kern,
	.map_poll = ringbuf_map_poll_kern,
	.map_lookup_elem = ringbuf_map_lookup_elem,
	.map_update_elem = ringbuf_map_update_elem,
	.map_delete_elem = ringbuf_map_delete_elem,
	.map_get_next_key = ringbuf_map_get_next_key,
	.map_btf_id = &ringbuf_map_btf_ids[0],
};

BTF_ID_LIST_SINGLE(user_ringbuf_map_btf_ids, struct, bpf_ringbuf_map)
const struct bpf_map_ops user_ringbuf_map_ops = {
	.map_meta_equal = bpf_map_meta_equal,
	.map_alloc = ringbuf_map_alloc,
	.map_free = ringbuf_map_free,
	.map_mmap = ringbuf_map_mmap_user,
	.map_poll = ringbuf_map_poll_user,
	.map_lookup_elem = ringbuf_map_lookup_elem,
	.map_update_elem = ringbuf_map_update_elem,
	.map_delete_elem = ringbuf_map_delete_elem,
	.map_get_next_key = ringbuf_map_get_next_key,
	.map_btf_id = &user_ringbuf_map_btf_ids[0],
};

/* Given pointer to ring buffer record metadata and struct bpf_ringbuf itself,
 * calculate offset from record metadata to ring buffer in pages, rounded
 * down. This page offset is stored as part of record metadata and allows to
 * restore struct bpf_ringbuf * from record pointer. This page offset is
 * stored at offset 4 of record metadata header.
 */
static size_t bpf_ringbuf_rec_pg_off(struct bpf_ringbuf *rb,
				     struct bpf_ringbuf_hdr *hdr)
{
	return ((void *)hdr - (void *)rb) >> PAGE_SHIFT;
}

/* Given pointer to ring buffer record header, restore pointer to struct
 * bpf_ringbuf itself by using page offset stored at offset 4
 */
static struct bpf_ringbuf *
bpf_ringbuf_restore_from_rec(struct bpf_ringbuf_hdr *hdr)
{
	unsigned long addr = (unsigned long)(void *)hdr;
	unsigned long off = (unsigned long)hdr->pg_off << PAGE_SHIFT;

	return (void*)((addr & PAGE_MASK) - off);
}

static void *__bpf_ringbuf_reserve(struct bpf_ringbuf *rb, u64 size)
{
	unsigned long cons_pos, prod_pos, new_prod_pos, flags;
	u32 len, pg_off;
	struct bpf_ringbuf_hdr *hdr;

	if (unlikely(size > RINGBUF_MAX_RECORD_SZ))
		return NULL;

	len = round_up(size + BPF_RINGBUF_HDR_SZ, 8);
	if (len > ringbuf_total_data_sz(rb))
		return NULL;

	cons_pos = smp_load_acquire(&rb->consumer_pos);

	if (in_nmi()) {
		if (!spin_trylock_irqsave(&rb->spinlock, flags))
			return NULL;
	} else {
		spin_lock_irqsave(&rb->spinlock, flags);
	}

	prod_pos = rb->producer_pos;
	new_prod_pos = prod_pos + len;

	/* check for out of ringbuf space by ensuring producer position
	 * doesn't advance more than (ringbuf_size - 1) ahead
	 */
	if (new_prod_pos - cons_pos > rb->mask) {
		spin_unlock_irqrestore(&rb->spinlock, flags);
		return NULL;
	}

	hdr = (void *)rb->data + (prod_pos & rb->mask);
	pg_off = bpf_ringbuf_rec_pg_off(rb, hdr);
	hdr->len = size | BPF_RINGBUF_BUSY_BIT;
	hdr->pg_off = pg_off;

	/* pairs with consumer's smp_load_acquire() */
	smp_store_release(&rb->producer_pos, new_prod_pos);

	spin_unlock_irqrestore(&rb->spinlock, flags);

	return (void *)hdr + BPF_RINGBUF_HDR_SZ;
}

BPF_CALL_3(bpf_ringbuf_reserve, struct bpf_map *, map, u64, size, u64, flags)
{
	struct bpf_ringbuf_map *rb_map;

	if (unlikely(flags))
		return 0;

	rb_map = container_of(map, struct bpf_ringbuf_map, map);
	return (unsigned long)__bpf_ringbuf_reserve(rb_map->rb, size);
}

const struct bpf_func_proto bpf_ringbuf_reserve_proto = {
	.func		= bpf_ringbuf_reserve,
	.ret_type	= RET_PTR_TO_ALLOC_MEM_OR_NULL,
	.arg1_type	= ARG_CONST_MAP_PTR,
	.arg2_type	= ARG_CONST_ALLOC_SIZE_OR_ZERO,
	.arg3_type	= ARG_ANYTHING,
};

static void bpf_ringbuf_commit(void *sample, u64 flags, bool discard)
{
	unsigned long rec_pos, cons_pos;
	struct bpf_ringbuf_hdr *hdr;
	struct bpf_ringbuf *rb;
	u32 new_len;

	hdr = sample - BPF_RINGBUF_HDR_SZ;
	rb = bpf_ringbuf_restore_from_rec(hdr);
	new_len = hdr->len ^ BPF_RINGBUF_BUSY_BIT;
	if (discard)
		new_len |= BPF_RINGBUF_DISCARD_BIT;

	/* update record header with correct final size prefix */
	xchg(&hdr->len, new_len);

	/* if consumer caught up and is waiting for our record, notify about
	 * new data availability
	 */
	rec_pos = (void *)hdr - (void *)rb->data;
	cons_pos = smp_load_acquire(&rb->consumer_pos) & rb->mask;

	if (flags & BPF_RB_FORCE_WAKEUP)
		irq_work_queue(&rb->work);
	else if (cons_pos == rec_pos && !(flags & BPF_RB_NO_WAKEUP))
		irq_work_queue(&rb->work);
}

BPF_CALL_2(bpf_ringbuf_submit, void *, sample, u64, flags)
{
	bpf_ringbuf_commit(sample, flags, false /* discard */);
	return 0;
}

const struct bpf_func_proto bpf_ringbuf_submit_proto = {
	.func		= bpf_ringbuf_submit,
	.ret_type	= RET_VOID,
	.arg1_type	= ARG_PTR_TO_ALLOC_MEM | OBJ_RELEASE,
	.arg2_type	= ARG_ANYTHING,
};

BPF_CALL_2(bpf_ringbuf_discard, void *, sample, u64, flags)
{
	bpf_ringbuf_commit(sample, flags, true /* discard */);
	return 0;
}

const struct bpf_func_proto bpf_ringbuf_discard_proto = {
	.func		= bpf_ringbuf_discard,
	.ret_type	= RET_VOID,
	.arg1_type	= ARG_PTR_TO_ALLOC_MEM | OBJ_RELEASE,
	.arg2_type	= ARG_ANYTHING,
};

BPF_CALL_4(bpf_ringbuf_output, struct bpf_map *, map, void *, data, u64, size,
	   u64, flags)
{
	struct bpf_ringbuf_map *rb_map;
	void *rec;

	if (unlikely(flags & ~(BPF_RB_NO_WAKEUP | BPF_RB_FORCE_WAKEUP)))
		return -EINVAL;

	rb_map = container_of(map, struct bpf_ringbuf_map, map);
	rec = __bpf_ringbuf_reserve(rb_map->rb, size);
	if (!rec)
		return -EAGAIN;

	memcpy(rec, data, size);
	bpf_ringbuf_commit(rec, flags, false /* discard */);
	return 0;
}

const struct bpf_func_proto bpf_ringbuf_output_proto = {
	.func		= bpf_ringbuf_output,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_CONST_MAP_PTR,
	.arg2_type	= ARG_PTR_TO_MEM | MEM_RDONLY,
	.arg3_type	= ARG_CONST_SIZE_OR_ZERO,
	.arg4_type	= ARG_ANYTHING,
};

BPF_CALL_2(bpf_ringbuf_query, struct bpf_map *, map, u64, flags)
{
	struct bpf_ringbuf *rb;

	rb = container_of(map, struct bpf_ringbuf_map, map)->rb;

	switch (flags) {
	case BPF_RB_AVAIL_DATA:
		return ringbuf_avail_data_sz(rb);
	case BPF_RB_RING_SIZE:
		return ringbuf_total_data_sz(rb);
	case BPF_RB_CONS_POS:
		return smp_load_acquire(&rb->consumer_pos);
	case BPF_RB_PROD_POS:
		return smp_load_acquire(&rb->producer_pos);
	default:
		return 0;
	}
}

const struct bpf_func_proto bpf_ringbuf_query_proto = {
	.func		= bpf_ringbuf_query,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_CONST_MAP_PTR,
	.arg2_type	= ARG_ANYTHING,
};

BPF_CALL_4(bpf_ringbuf_reserve_dynptr, struct bpf_map *, map, u32, size, u64, flags,
	   struct bpf_dynptr_kern *, ptr)
{
	struct bpf_ringbuf_map *rb_map;
	void *sample;
	int err;

	if (unlikely(flags)) {
		bpf_dynptr_set_null(ptr);
		return -EINVAL;
	}

	err = bpf_dynptr_check_size(size);
	if (err) {
		bpf_dynptr_set_null(ptr);
		return err;
	}

	rb_map = container_of(map, struct bpf_ringbuf_map, map);

	sample = __bpf_ringbuf_reserve(rb_map->rb, size);
	if (!sample) {
		bpf_dynptr_set_null(ptr);
		return -EINVAL;
	}

	bpf_dynptr_init(ptr, sample, BPF_DYNPTR_TYPE_RINGBUF, 0, size);

	return 0;
}

const struct bpf_func_proto bpf_ringbuf_reserve_dynptr_proto = {
	.func		= bpf_ringbuf_reserve_dynptr,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_CONST_MAP_PTR,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_PTR_TO_DYNPTR | DYNPTR_TYPE_RINGBUF | MEM_UNINIT,
};

BPF_CALL_2(bpf_ringbuf_submit_dynptr, struct bpf_dynptr_kern *, ptr, u64, flags)
{
	if (!ptr->data)
		return 0;

	bpf_ringbuf_commit(ptr->data, flags, false /* discard */);

	bpf_dynptr_set_null(ptr);

	return 0;
}

const struct bpf_func_proto bpf_ringbuf_submit_dynptr_proto = {
	.func		= bpf_ringbuf_submit_dynptr,
	.ret_type	= RET_VOID,
	.arg1_type	= ARG_PTR_TO_DYNPTR | DYNPTR_TYPE_RINGBUF | OBJ_RELEASE,
	.arg2_type	= ARG_ANYTHING,
};

BPF_CALL_2(bpf_ringbuf_discard_dynptr, struct bpf_dynptr_kern *, ptr, u64, flags)
{
	if (!ptr->data)
		return 0;

	bpf_ringbuf_commit(ptr->data, flags, true /* discard */);

	bpf_dynptr_set_null(ptr);

	return 0;
}

const struct bpf_func_proto bpf_ringbuf_discard_dynptr_proto = {
	.func		= bpf_ringbuf_discard_dynptr,
	.ret_type	= RET_VOID,
	.arg1_type	= ARG_PTR_TO_DYNPTR | DYNPTR_TYPE_RINGBUF | OBJ_RELEASE,
	.arg2_type	= ARG_ANYTHING,
};

static int __bpf_user_ringbuf_peek(struct bpf_ringbuf *rb, void **sample, u32 *size)
{
	int err;
	u32 hdr_len, sample_len, total_len, flags, *hdr;
	u64 cons_pos, prod_pos;

	/* Synchronizes with smp_store_release() in user-space producer. */
	prod_pos = smp_load_acquire(&rb->producer_pos);
	if (prod_pos % 8)
		return -EINVAL;

	/* Synchronizes with smp_store_release() in __bpf_user_ringbuf_sample_release() */
	cons_pos = smp_load_acquire(&rb->consumer_pos);
	if (cons_pos >= prod_pos)
		return -ENODATA;

	hdr = (u32 *)((uintptr_t)rb->data + (uintptr_t)(cons_pos & rb->mask));
	/* Synchronizes with smp_store_release() in user-space producer. */
	hdr_len = smp_load_acquire(hdr);
	flags = hdr_len & (BPF_RINGBUF_BUSY_BIT | BPF_RINGBUF_DISCARD_BIT);
	sample_len = hdr_len & ~flags;
	total_len = round_up(sample_len + BPF_RINGBUF_HDR_SZ, 8);

	/* The sample must fit within the region advertised by the producer position. */
	if (total_len > prod_pos - cons_pos)
		return -EINVAL;

	/* The sample must fit within the data region of the ring buffer. */
	if (total_len > ringbuf_total_data_sz(rb))
		return -E2BIG;

	/* The sample must fit into a struct bpf_dynptr. */
	err = bpf_dynptr_check_size(sample_len);
	if (err)
		return -E2BIG;

	if (flags & BPF_RINGBUF_DISCARD_BIT) {
		/* If the discard bit is set, the sample should be skipped.
		 *
		 * Update the consumer pos, and return -EAGAIN so the caller
		 * knows to skip this sample and try to read the next one.
		 */
		smp_store_release(&rb->consumer_pos, cons_pos + total_len);
		return -EAGAIN;
	}

	if (flags & BPF_RINGBUF_BUSY_BIT)
		return -ENODATA;

	*sample = (void *)((uintptr_t)rb->data +
			   (uintptr_t)((cons_pos + BPF_RINGBUF_HDR_SZ) & rb->mask));
	*size = sample_len;
	return 0;
}

static void __bpf_user_ringbuf_sample_release(struct bpf_ringbuf *rb, size_t size, u64 flags)
{
	u64 consumer_pos;
	u32 rounded_size = round_up(size + BPF_RINGBUF_HDR_SZ, 8);

	/* Using smp_load_acquire() is unnecessary here, as the busy-bit
	 * prevents another task from writing to consumer_pos after it was read
	 * by this task with smp_load_acquire() in __bpf_user_ringbuf_peek().
	 */
	consumer_pos = rb->consumer_pos;
	 /* Synchronizes with smp_load_acquire() in user-space producer. */
	smp_store_release(&rb->consumer_pos, consumer_pos + rounded_size);
}

BPF_CALL_4(bpf_user_ringbuf_drain, struct bpf_map *, map,
	   void *, callback_fn, void *, callback_ctx, u64, flags)
{
	struct bpf_ringbuf *rb;
	long samples, discarded_samples = 0, ret = 0;
	bpf_callback_t callback = (bpf_callback_t)callback_fn;
	u64 wakeup_flags = BPF_RB_NO_WAKEUP | BPF_RB_FORCE_WAKEUP;
	int busy = 0;

	if (unlikely(flags & ~wakeup_flags))
		return -EINVAL;

	rb = container_of(map, struct bpf_ringbuf_map, map)->rb;

	/* If another consumer is already consuming a sample, wait for them to finish. */
	if (!atomic_try_cmpxchg(&rb->busy, &busy, 1))
		return -EBUSY;

	for (samples = 0; samples < BPF_MAX_USER_RINGBUF_SAMPLES && ret == 0; samples++) {
		int err;
		u32 size;
		void *sample;
		struct bpf_dynptr_kern dynptr;

		err = __bpf_user_ringbuf_peek(rb, &sample, &size);
		if (err) {
			if (err == -ENODATA) {
				break;
			} else if (err == -EAGAIN) {
				discarded_samples++;
				continue;
			} else {
				ret = err;
				goto schedule_work_return;
			}
		}

		bpf_dynptr_init(&dynptr, sample, BPF_DYNPTR_TYPE_LOCAL, 0, size);
		ret = callback((uintptr_t)&dynptr, (uintptr_t)callback_ctx, 0, 0, 0);
		__bpf_user_ringbuf_sample_release(rb, size, flags);
	}
	ret = samples - discarded_samples;

schedule_work_return:
	/* Prevent the clearing of the busy-bit from being reordered before the
	 * storing of any rb consumer or producer positions.
	 */
	smp_mb__before_atomic();
	atomic_set(&rb->busy, 0);

	if (flags & BPF_RB_FORCE_WAKEUP)
		irq_work_queue(&rb->work);
	else if (!(flags & BPF_RB_NO_WAKEUP) && samples > 0)
		irq_work_queue(&rb->work);
	return ret;
}

const struct bpf_func_proto bpf_user_ringbuf_drain_proto = {
	.func		= bpf_user_ringbuf_drain,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_CONST_MAP_PTR,
	.arg2_type	= ARG_PTR_TO_FUNC,
	.arg3_type	= ARG_PTR_TO_STACK_OR_NULL,
	.arg4_type	= ARG_ANYTHING,
};
