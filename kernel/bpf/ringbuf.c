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
#include <uapi/linux/btf.h>

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
	/* Consumer and producer counters are put into separate pages to allow
	 * mapping consumer page as r/w, but restrict producer page to r/o.
	 * This protects producer position from being modified by user-space
	 * application and ruining in-kernel position tracking.
	 */
	unsigned long consumer_pos __aligned(PAGE_SIZE);
	unsigned long producer_pos __aligned(PAGE_SIZE);
	char data[] __aligned(PAGE_SIZE);
};

struct bpf_ringbuf_map {
	struct bpf_map map;
	struct bpf_map_memory memory;
	struct bpf_ringbuf *rb;
};

/* 8-byte ring buffer record header structure */
struct bpf_ringbuf_hdr {
	u32 len;
	u32 pg_off;
};

static struct bpf_ringbuf *bpf_ringbuf_area_alloc(size_t data_sz, int numa_node)
{
	const gfp_t flags = GFP_KERNEL | __GFP_RETRY_MAYFAIL | __GFP_NOWARN |
			    __GFP_ZERO;
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
	if (array_size > PAGE_SIZE)
		pages = vmalloc_node(array_size, numa_node);
	else
		pages = kmalloc_node(array_size, flags, numa_node);
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
		  VM_ALLOC | VM_USERMAP, PAGE_KERNEL);
	if (rb) {
		rb->pages = pages;
		rb->nr_pages = nr_pages;
		return rb;
	}

err_free_pages:
	for (i = 0; i < nr_pages; i++)
		__free_page(pages[i]);
	kvfree(pages);
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
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&rb->spinlock);
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
	u64 cost;
	int err;

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

	rb_map = kzalloc(sizeof(*rb_map), GFP_USER);
	if (!rb_map)
		return ERR_PTR(-ENOMEM);

	bpf_map_init_from_attr(&rb_map->map, attr);

	cost = sizeof(struct bpf_ringbuf_map) +
	       sizeof(struct bpf_ringbuf) +
	       attr->max_entries;
	err = bpf_map_charge_init(&rb_map->map.memory, cost);
	if (err)
		goto err_free_map;

	rb_map->rb = bpf_ringbuf_alloc(attr->max_entries, rb_map->map.numa_node);
	if (IS_ERR(rb_map->rb)) {
		err = PTR_ERR(rb_map->rb);
		goto err_uncharge;
	}

	return &rb_map->map;

err_uncharge:
	bpf_map_charge_finish(&rb_map->map.memory);
err_free_map:
	kfree(rb_map);
	return ERR_PTR(err);
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
	kvfree(pages);
}

static void ringbuf_map_free(struct bpf_map *map)
{
	struct bpf_ringbuf_map *rb_map;

	rb_map = container_of(map, struct bpf_ringbuf_map, map);
	bpf_ringbuf_free(rb_map->rb);
	kfree(rb_map);
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

static size_t bpf_ringbuf_mmap_page_cnt(const struct bpf_ringbuf *rb)
{
	size_t data_pages = (rb->mask + 1) >> PAGE_SHIFT;

	/* consumer page + producer page + 2 x data pages */
	return RINGBUF_POS_PAGES + 2 * data_pages;
}

static int ringbuf_map_mmap(struct bpf_map *map, struct vm_area_struct *vma)
{
	struct bpf_ringbuf_map *rb_map;
	size_t mmap_sz;

	rb_map = container_of(map, struct bpf_ringbuf_map, map);
	mmap_sz = bpf_ringbuf_mmap_page_cnt(rb_map->rb) << PAGE_SHIFT;

	if (vma->vm_pgoff * PAGE_SIZE + (vma->vm_end - vma->vm_start) > mmap_sz)
		return -EINVAL;

	return remap_vmalloc_range(vma, rb_map->rb,
				   vma->vm_pgoff + RINGBUF_PGOFF);
}

static unsigned long ringbuf_avail_data_sz(struct bpf_ringbuf *rb)
{
	unsigned long cons_pos, prod_pos;

	cons_pos = smp_load_acquire(&rb->consumer_pos);
	prod_pos = smp_load_acquire(&rb->producer_pos);
	return prod_pos - cons_pos;
}

static __poll_t ringbuf_map_poll(struct bpf_map *map, struct file *filp,
				 struct poll_table_struct *pts)
{
	struct bpf_ringbuf_map *rb_map;

	rb_map = container_of(map, struct bpf_ringbuf_map, map);
	poll_wait(filp, &rb_map->rb->waitq, pts);

	if (ringbuf_avail_data_sz(rb_map->rb))
		return EPOLLIN | EPOLLRDNORM;
	return 0;
}

static int ringbuf_map_btf_id;
const struct bpf_map_ops ringbuf_map_ops = {
	.map_meta_equal = bpf_map_meta_equal,
	.map_alloc = ringbuf_map_alloc,
	.map_free = ringbuf_map_free,
	.map_mmap = ringbuf_map_mmap,
	.map_poll = ringbuf_map_poll,
	.map_lookup_elem = ringbuf_map_lookup_elem,
	.map_update_elem = ringbuf_map_update_elem,
	.map_delete_elem = ringbuf_map_delete_elem,
	.map_get_next_key = ringbuf_map_get_next_key,
	.map_btf_name = "bpf_ringbuf_map",
	.map_btf_id = &ringbuf_map_btf_id,
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
	.arg1_type	= ARG_PTR_TO_ALLOC_MEM,
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
	.arg1_type	= ARG_PTR_TO_ALLOC_MEM,
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
	.arg2_type	= ARG_PTR_TO_MEM,
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
		return rb->mask + 1;
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
