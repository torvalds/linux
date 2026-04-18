// SPDX-License-Identifier: GPL-2.0
/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#include "protocol.h"
#include "orangefs-kernel.h"
#include "orangefs-bufmap.h"

struct slot_map {
	int c;
	wait_queue_head_t q;
	int count;
	unsigned long *map;
};

static struct slot_map rw_map = {
	.c = -1,
	.q = __WAIT_QUEUE_HEAD_INITIALIZER(rw_map.q)
};
static struct slot_map readdir_map = {
	.c = -1,
	.q = __WAIT_QUEUE_HEAD_INITIALIZER(readdir_map.q)
};


static void install(struct slot_map *m, int count, unsigned long *map)
{
	spin_lock(&m->q.lock);
	m->c = m->count = count;
	m->map = map;
	wake_up_all_locked(&m->q);
	spin_unlock(&m->q.lock);
}

static void mark_killed(struct slot_map *m)
{
	spin_lock(&m->q.lock);
	m->c -= m->count + 1;
	spin_unlock(&m->q.lock);
}

static void run_down(struct slot_map *m)
{
	DEFINE_WAIT(wait);
	spin_lock(&m->q.lock);
	if (m->c != -1) {
		for (;;) {
			if (likely(list_empty(&wait.entry)))
				__add_wait_queue_entry_tail(&m->q, &wait);
			set_current_state(TASK_UNINTERRUPTIBLE);

			if (m->c == -1)
				break;

			spin_unlock(&m->q.lock);
			schedule();
			spin_lock(&m->q.lock);
		}
		__remove_wait_queue(&m->q, &wait);
		__set_current_state(TASK_RUNNING);
	}
	m->map = NULL;
	spin_unlock(&m->q.lock);
}

static void put(struct slot_map *m, int slot)
{
	int v;
	spin_lock(&m->q.lock);
	__clear_bit(slot, m->map);
	v = ++m->c;
	if (v > 0)
		wake_up_locked(&m->q);
	if (unlikely(v == -1))     /* finished dying */
		wake_up_all_locked(&m->q);
	spin_unlock(&m->q.lock);
}

static int wait_for_free(struct slot_map *m)
{
	long left = slot_timeout_secs * HZ;
	DEFINE_WAIT(wait);

	do {
		long n = left, t;
		if (likely(list_empty(&wait.entry)))
			__add_wait_queue_entry_tail_exclusive(&m->q, &wait);
		set_current_state(TASK_INTERRUPTIBLE);

		if (m->c > 0)
			break;

		if (m->c < 0) {
			/* we are waiting for map to be installed */
			/* it would better be there soon, or we go away */
			if (n > ORANGEFS_BUFMAP_WAIT_TIMEOUT_SECS * HZ)
				n = ORANGEFS_BUFMAP_WAIT_TIMEOUT_SECS * HZ;
		}
		spin_unlock(&m->q.lock);
		t = schedule_timeout(n);
		spin_lock(&m->q.lock);
		if (unlikely(!t) && n != left && m->c < 0)
			left = t;
		else
			left = t + (left - n);
		if (signal_pending(current))
			left = -EINTR;
	} while (left > 0);

	if (!list_empty(&wait.entry))
		list_del(&wait.entry);
	else if (left <= 0 && waitqueue_active(&m->q))
		__wake_up_locked_key(&m->q, TASK_INTERRUPTIBLE, NULL);
	__set_current_state(TASK_RUNNING);

	if (likely(left > 0))
		return 0;

	return left < 0 ? -EINTR : -ETIMEDOUT;
}

static int get(struct slot_map *m)
{
	int res = 0;
	spin_lock(&m->q.lock);
	if (unlikely(m->c <= 0))
		res = wait_for_free(m);
	if (likely(!res)) {
		m->c--;
		res = find_first_zero_bit(m->map, m->count);
		__set_bit(res, m->map);
	}
	spin_unlock(&m->q.lock);
	return res;
}

/* used to describe mapped buffers */
struct orangefs_bufmap_desc {
	void __user *uaddr;		/* user space address pointer */
	struct folio **folio_array;
	/*
	 * folio_offsets could be needed when userspace sets custom
	 * sizes in user_desc, or when folios aren't all backed by
	 * 2MB THPs.
	 */
	size_t *folio_offsets;
	int folio_count;
	bool is_two_2mib_chunks;
};

static struct orangefs_bufmap {
	int desc_size;
	int desc_shift;
	int desc_count;
	int total_size;
	int page_count;
	int folio_count;

	struct page **page_array;
	struct folio **folio_array;
	struct orangefs_bufmap_desc *desc_array;

	/* array to track usage of buffer descriptors */
	unsigned long *buffer_index_array;

	/* array to track usage of buffer descriptors for readdir */
#define N DIV_ROUND_UP(ORANGEFS_READDIR_DEFAULT_DESC_COUNT, BITS_PER_LONG)
	unsigned long readdir_index_array[N];
#undef N
} *__orangefs_bufmap;

static DEFINE_SPINLOCK(orangefs_bufmap_lock);

static void
orangefs_bufmap_unmap(struct orangefs_bufmap *bufmap)
{
	unpin_user_pages(bufmap->page_array, bufmap->page_count);
}

static void
orangefs_bufmap_free(struct orangefs_bufmap *bufmap)
{
	int i;

	if (!bufmap)
		return;

	for (i = 0; i < bufmap->desc_count; i++) {
		kfree(bufmap->desc_array[i].folio_array);
		kfree(bufmap->desc_array[i].folio_offsets);
		bufmap->desc_array[i].folio_array = NULL;
		bufmap->desc_array[i].folio_offsets = NULL;
	}
	kfree(bufmap->page_array);
	kfree(bufmap->desc_array);
	bitmap_free(bufmap->buffer_index_array);
	kfree(bufmap);
}

/*
 * XXX: Can the size and shift change while the caller gives up the
 * XXX: lock between calling this and doing something useful?
 */

int orangefs_bufmap_size_query(void)
{
	struct orangefs_bufmap *bufmap;
	int size = 0;
	spin_lock(&orangefs_bufmap_lock);
	bufmap = __orangefs_bufmap;
	if (bufmap)
		size = bufmap->desc_size;
	spin_unlock(&orangefs_bufmap_lock);
	return size;
}

static DECLARE_WAIT_QUEUE_HEAD(bufmap_waitq);
static DECLARE_WAIT_QUEUE_HEAD(readdir_waitq);

static struct orangefs_bufmap *
orangefs_bufmap_alloc(struct ORANGEFS_dev_map_desc *user_desc)
{
	struct orangefs_bufmap *bufmap;

	bufmap = kzalloc_obj(*bufmap);
	if (!bufmap)
		goto out;

	bufmap->total_size = user_desc->total_size;
	bufmap->desc_count = user_desc->count;
	bufmap->desc_size = user_desc->size;
	bufmap->desc_shift = ilog2(bufmap->desc_size);
	bufmap->page_count = bufmap->total_size / PAGE_SIZE;

	bufmap->buffer_index_array =
		bitmap_zalloc(bufmap->desc_count, GFP_KERNEL);
	if (!bufmap->buffer_index_array)
		goto out_free_bufmap;

	bufmap->desc_array =
		kzalloc_objs(struct orangefs_bufmap_desc, bufmap->desc_count);
	if (!bufmap->desc_array)
		goto out_free_index_array;

	/* allocate storage to track our page mappings */
	bufmap->page_array =
		kzalloc_objs(struct page *, bufmap->page_count);
	if (!bufmap->page_array)
		goto out_free_desc_array;

	/* allocate folio array. */
	bufmap->folio_array = kzalloc_objs(struct folio *, bufmap->page_count);
	if (!bufmap->folio_array)
		goto out_free_page_array;

	return bufmap;

out_free_page_array:
	kfree(bufmap->page_array);
out_free_desc_array:
	kfree(bufmap->desc_array);
out_free_index_array:
	bitmap_free(bufmap->buffer_index_array);
out_free_bufmap:
	kfree(bufmap);
out:
	return NULL;
}

static int orangefs_bufmap_group_folios(struct orangefs_bufmap *bufmap)
{
	int i = 0;
	int f = 0;
	int k;
	int num_pages;
	struct page *page;
	struct folio *folio;

	while (i < bufmap->page_count) {
		page = bufmap->page_array[i];
		folio = page_folio(page);
		num_pages = folio_nr_pages(folio);
		gossip_debug(GOSSIP_BUFMAP_DEBUG,
			"%s: i:%d: num_pages:%d: \n", __func__, i, num_pages);

		for (k = 1; k < num_pages; k++) {
			if (bufmap->page_array[i + k] != folio_page(folio, k)) {
				gossip_err("%s: bad match,  i:%d: k:%d:\n",
					__func__, i, k);
				return -EINVAL;
			}
		}

		bufmap->folio_array[f++] = folio;
		i += num_pages;
	}

	bufmap->folio_count = f;
	pr_info("%s: Grouped %d folios from %d pages.\n",
		__func__,
		bufmap->folio_count,
		bufmap->page_count);
	return 0;
}

static int orangefs_bufmap_map(struct orangefs_bufmap *bufmap,
				struct ORANGEFS_dev_map_desc *user_desc)
{
	int pages_per_desc = bufmap->desc_size / PAGE_SIZE;
	int ret;
	int i;
	int j;
	int current_folio;
	int desc_pages_needed;
	int desc_folio_count;
	int remaining_pages;
	int need_avail_min;
	int pages_assigned_to_this_desc;
	int allocated_descs = 0;
	size_t current_offset;
	size_t adjust_offset;
	struct folio *folio;

	/* map the pages */
	ret = pin_user_pages_fast((unsigned long)user_desc->ptr,
		bufmap->page_count,
		FOLL_WRITE,
		bufmap->page_array);

	if (ret < 0)
		return ret;

	if (ret != bufmap->page_count) {
		gossip_err("orangefs error: asked for %d pages, only got %d.\n",
				bufmap->page_count, ret);
		for (i = 0; i < ret; i++)
			unpin_user_page(bufmap->page_array[i]);
		return -ENOMEM;
	}

	/*
	 * ideally we want to get kernel space pointers for each page, but
	 * we can't kmap that many pages at once if highmem is being used.
	 * so instead, we just kmap/kunmap the page address each time the
	 * kaddr is needed.
	 */
	for (i = 0; i < bufmap->page_count; i++)
		flush_dcache_page(bufmap->page_array[i]);

	/*
	 * Group pages into folios.
	 */
	ret = orangefs_bufmap_group_folios(bufmap);
	if (ret)
		goto unpin;

	pr_info("%s: desc_size=%d bytes (%d pages per desc), total folios=%d\n",
			__func__, bufmap->desc_size, pages_per_desc,
			bufmap->folio_count);

	current_folio = 0;
	remaining_pages = 0;
	current_offset = 0;
	for (i = 0; i < bufmap->desc_count; i++) {
		desc_pages_needed = pages_per_desc;
		desc_folio_count = 0;
		pages_assigned_to_this_desc = 0;
		bufmap->desc_array[i].is_two_2mib_chunks = false;

		/*
		 * We hope there was enough memory that each desc is
		 * covered by two THPs/folios, if not we want to keep on
		 * working even if there's only one page per folio.
		 */
		bufmap->desc_array[i].folio_array =
			kzalloc_objs(struct folio *, pages_per_desc);
		if (!bufmap->desc_array[i].folio_array) {
			ret = -ENOMEM;
			goto unpin;
		}

		bufmap->desc_array[i].folio_offsets =
			kzalloc_objs(size_t, pages_per_desc);
		if (!bufmap->desc_array[i].folio_offsets) {
			ret = -ENOMEM;
			kfree(bufmap->desc_array[i].folio_array);
			bufmap->desc_array[i].folio_array = NULL;
			goto unpin;
		}

		bufmap->desc_array[i].uaddr =
			user_desc->ptr + (size_t)i * bufmap->desc_size;

		/*
		 * Accumulate folios until desc is full.
		 */
		while (desc_pages_needed > 0) {
			if (remaining_pages == 0) {
				/* shouldn't happen. */
				if (current_folio >= bufmap->folio_count) {
					ret = -EINVAL;
					goto unpin;
				}
				folio = bufmap->folio_array[current_folio++];
				remaining_pages = folio_nr_pages(folio);
				current_offset = 0;
			} else {
				folio = bufmap->folio_array[current_folio - 1];
			}

			need_avail_min =
				min(desc_pages_needed, remaining_pages);
			adjust_offset = need_avail_min * PAGE_SIZE;

			bufmap->desc_array[i].folio_array[desc_folio_count] =
				folio;
			bufmap->desc_array[i].folio_offsets[desc_folio_count] =
				current_offset;
			desc_folio_count++;
			pages_assigned_to_this_desc += need_avail_min;
			desc_pages_needed -= need_avail_min;
			remaining_pages -= need_avail_min;
			current_offset += adjust_offset;
		}

		/* Detect optimal case: two 2MiB folios per 4MiB slot. */
		if (desc_folio_count == 2 &&
		  folio_nr_pages(bufmap->desc_array[i].folio_array[0]) == 512 &&
		  folio_nr_pages(bufmap->desc_array[i].folio_array[1]) == 512) {
			bufmap->desc_array[i].is_two_2mib_chunks = true;
			gossip_debug(GOSSIP_BUFMAP_DEBUG, "%s: descriptor :%d: "
				"optimal folio/page ratio.\n", __func__, i);
		}

		bufmap->desc_array[i].folio_count = desc_folio_count;
		gossip_debug(GOSSIP_BUFMAP_DEBUG,
			" descriptor %d: folio_count=%d, "
			"pages_assigned=%d (should be %d)\n",
			i, desc_folio_count, pages_assigned_to_this_desc,
			pages_per_desc);

		allocated_descs = i + 1;
	}

	return 0;
unpin:
	/*
	 * rollback any allocations we got so far...
	 * Memory pressure, like in generic/340, led me
	 * to write the rollback this way.
	 */
	for (j = 0; j < allocated_descs; j++) {
		if (bufmap->desc_array[j].folio_array) {
			kfree(bufmap->desc_array[j].folio_array);
			bufmap->desc_array[j].folio_array = NULL;
		}
		if (bufmap->desc_array[j].folio_offsets) {
			kfree(bufmap->desc_array[j].folio_offsets);
			bufmap->desc_array[j].folio_offsets = NULL;
		}
	}
	unpin_user_pages(bufmap->page_array, bufmap->page_count);
	return ret;
}

/*
 * orangefs_bufmap_initialize()
 *
 * initializes the mapped buffer interface
 *
 * user_desc is the parameters provided by userspace for the bufmap.
 *
 * returns 0 on success, -errno on failure
 */
int orangefs_bufmap_initialize(struct ORANGEFS_dev_map_desc *user_desc)
{
	struct orangefs_bufmap *bufmap;
	int ret = -EINVAL;

	gossip_debug(GOSSIP_BUFMAP_DEBUG,
		     "%s: called (ptr (" "%p) sz (%d) cnt(%d).\n",
		     __func__,
		     user_desc->ptr,
		     user_desc->size,
		     user_desc->count);

	if (user_desc->total_size < 0 ||
	    user_desc->size < 0 ||
	    user_desc->count < 0)
		goto out;

	/*
	 * sanity check alignment and size of buffer that caller wants to
	 * work with
	 */
	if (PAGE_ALIGN((unsigned long)user_desc->ptr) !=
	    (unsigned long)user_desc->ptr) {
		gossip_err("orangefs error: memory alignment (front). %p\n",
			   user_desc->ptr);
		goto out;
	}

	if (PAGE_ALIGN(((unsigned long)user_desc->ptr + user_desc->total_size))
	    != (unsigned long)(user_desc->ptr + user_desc->total_size)) {
		gossip_err("orangefs error: memory alignment (back).(%p + %d)\n",
			   user_desc->ptr,
			   user_desc->total_size);
		goto out;
	}

	if (user_desc->total_size != (user_desc->size * user_desc->count)) {
		gossip_err("orangefs error: user provided an oddly sized buffer: (%d, %d, %d)\n",
			   user_desc->total_size,
			   user_desc->size,
			   user_desc->count);
		goto out;
	}

	if ((user_desc->size % PAGE_SIZE) != 0) {
		gossip_err("orangefs error: bufmap size not page size divisible (%d).\n",
			   user_desc->size);
		goto out;
	}

	ret = -ENOMEM;
	bufmap = orangefs_bufmap_alloc(user_desc);
	if (!bufmap)
		goto out;

	ret = orangefs_bufmap_map(bufmap, user_desc);
	if (ret)
		goto out_free_bufmap;


	spin_lock(&orangefs_bufmap_lock);
	if (__orangefs_bufmap) {
		spin_unlock(&orangefs_bufmap_lock);
		gossip_err("orangefs: error: bufmap already initialized.\n");
		ret = -EINVAL;
		goto out_unmap_bufmap;
	}
	__orangefs_bufmap = bufmap;
	install(&rw_map,
		bufmap->desc_count,
		bufmap->buffer_index_array);
	install(&readdir_map,
		ORANGEFS_READDIR_DEFAULT_DESC_COUNT,
		bufmap->readdir_index_array);
	spin_unlock(&orangefs_bufmap_lock);

	gossip_debug(GOSSIP_BUFMAP_DEBUG,
		     "%s: exiting normally\n", __func__);
	return 0;

out_unmap_bufmap:
	orangefs_bufmap_unmap(bufmap);
out_free_bufmap:
	orangefs_bufmap_free(bufmap);
out:
	return ret;
}

/*
 * orangefs_bufmap_finalize()
 *
 * shuts down the mapped buffer interface and releases any resources
 * associated with it
 *
 * no return value
 */
void orangefs_bufmap_finalize(void)
{
	struct orangefs_bufmap *bufmap = __orangefs_bufmap;
	if (!bufmap)
		return;
	gossip_debug(GOSSIP_BUFMAP_DEBUG, "orangefs_bufmap_finalize: called\n");
	mark_killed(&rw_map);
	mark_killed(&readdir_map);
	gossip_debug(GOSSIP_BUFMAP_DEBUG,
		     "orangefs_bufmap_finalize: exiting normally\n");
}

void orangefs_bufmap_run_down(void)
{
	struct orangefs_bufmap *bufmap = __orangefs_bufmap;
	if (!bufmap)
		return;
	run_down(&rw_map);
	run_down(&readdir_map);
	spin_lock(&orangefs_bufmap_lock);
	__orangefs_bufmap = NULL;
	spin_unlock(&orangefs_bufmap_lock);
	orangefs_bufmap_unmap(bufmap);
	orangefs_bufmap_free(bufmap);
}

/*
 * orangefs_bufmap_get()
 *
 * gets a free mapped buffer descriptor, will sleep until one becomes
 * available if necessary
 *
 * returns slot on success, -errno on failure
 */
int orangefs_bufmap_get(void)
{
	return get(&rw_map);
}

/*
 * orangefs_bufmap_put()
 *
 * returns a mapped buffer descriptor to the collection
 *
 * no return value
 */
void orangefs_bufmap_put(int buffer_index)
{
	put(&rw_map, buffer_index);
}

/*
 * orangefs_readdir_index_get()
 *
 * gets a free descriptor, will sleep until one becomes
 * available if necessary.
 * Although the readdir buffers are not mapped into kernel space
 * we could do that at a later point of time. Regardless, these
 * indices are used by the client-core.
 *
 * returns slot on success, -errno on failure
 */
int orangefs_readdir_index_get(void)
{
	return get(&readdir_map);
}

void orangefs_readdir_index_put(int buffer_index)
{
	put(&readdir_map, buffer_index);
}

/*
 * we've been handed an iovec, we need to copy it to
 * the shared memory descriptor at "buffer_index".
 */
int orangefs_bufmap_copy_from_iovec(struct iov_iter *iter,
				int buffer_index,
				size_t size)
{
	struct orangefs_bufmap_desc *to;
	size_t remaining = size;
	int folio_index = 0;
	struct folio *folio;
	size_t folio_offset;
	size_t folio_avail;
	size_t copy_amount;
	size_t copied;
	void *kaddr;
	size_t half;
	size_t first;
	size_t second;

	to = &__orangefs_bufmap->desc_array[buffer_index];

	/* shouldn't happen... */
	if (size > 4194304)
		pr_info("%s: size:%zu\n", __func__, size);

	gossip_debug(GOSSIP_BUFMAP_DEBUG,
		"%s: buffer_index:%d size:%zu folio_count:%d\n",
		__func__,
		buffer_index,
		size,
		to->folio_count);

	/* Fast path: exactly two 2 MiB folios */
	if (to->is_two_2mib_chunks && size <= 4194304) {
		gossip_debug(GOSSIP_BUFMAP_DEBUG,
			"%s: fastpath hit.\n", __func__);
		half = 2097152;		/* 2 MiB */
		first = min(size, half);
		second = (size > half) ? size - half : 0;

		/* First 2 MiB chunk */
		kaddr = kmap_local_folio(to->folio_array[0], 0);
		copied = copy_from_iter(kaddr, first, iter);
		kunmap_local(kaddr);
		if (copied != first)
			return -EFAULT;

		if (second == 0)
			return 0;

		/* Second 2 MiB chunk */
		kaddr = kmap_local_folio(to->folio_array[1], 0);
		copied = copy_from_iter(kaddr, second, iter);
		kunmap_local(kaddr);
		if (copied != second)
			return -EFAULT;

		return 0;
	}

	while (remaining > 0) {

		if (unlikely(folio_index >= to->folio_count ||
			to->folio_array[folio_index] == NULL)) {
				gossip_err("%s: "
				   "folio_index:%d: >= folio_count:%d: "
		                   "(size %zu, buffer %d)\n",
					__func__,
					folio_index,
					to->folio_count,
					size,
					buffer_index);
				return -EFAULT;
		}

		folio = to->folio_array[folio_index];
		folio_offset = to->folio_offsets[folio_index];
		folio_avail = folio_nr_pages(folio) * PAGE_SIZE - folio_offset;
		copy_amount = min(remaining, folio_avail);
		kaddr = kmap_local_folio(folio, folio_offset);
		copied = copy_from_iter(kaddr, copy_amount, iter);
		kunmap_local(kaddr);

		if (copied != copy_amount)
			return -EFAULT;

		remaining -= copied;
		folio_index++;
	}

	return 0;
}

/*
 * we've been handed an iovec, we need to fill it from
 * the shared memory descriptor at "buffer_index".
 */
int orangefs_bufmap_copy_to_iovec(struct iov_iter *iter,
				    int buffer_index,
				    size_t size)
{
	struct orangefs_bufmap_desc *from;
	size_t remaining = size;
	int folio_index = 0;
	struct folio *folio;
	size_t folio_offset;
	size_t folio_avail;
	size_t copy_amount;
	size_t copied;
	void *kaddr;
	size_t half;
	size_t first;
	size_t second;

	from = &__orangefs_bufmap->desc_array[buffer_index];

	/* shouldn't happen... */
	if (size > 4194304)
		pr_info("%s: size:%zu\n", __func__, size);

	gossip_debug(GOSSIP_BUFMAP_DEBUG,
		"%s: buffer_index:%d size:%zu folio_count:%d\n",
		__func__,
		buffer_index,
		size,
		from->folio_count);

	/* Fast path: exactly two 2 MiB folios */
	if (from->is_two_2mib_chunks && size <= 4194304) {
		gossip_debug(GOSSIP_BUFMAP_DEBUG,
			"%s: fastpath hit.\n", __func__);
		half = 2097152;		/* 2 MiB */
		first = min(size, half);
		second = (size > half) ? size - half : 0;
		void *kaddr;
		size_t copied;

		/* First 2 MiB chunk */
		kaddr = kmap_local_folio(from->folio_array[0], 0);
		copied = copy_to_iter(kaddr, first, iter);
		kunmap_local(kaddr);
		if (copied != first)
			return -EFAULT;

		if (second == 0)
			return 0;

		/* Second 2 MiB chunk */
		kaddr = kmap_local_folio(from->folio_array[1], 0);
		copied = copy_to_iter(kaddr, second, iter);
		kunmap_local(kaddr);
		if (copied != second)
			return -EFAULT;

		return 0;
	}

	while (remaining > 0) {

		if (unlikely(folio_index >= from->folio_count ||
			from->folio_array[folio_index] == NULL)) {
				gossip_err("%s: "
				   "folio_index:%d: >= folio_count:%d: "
		                   "(size %zu, buffer %d)\n",
					__func__,
					folio_index,
					from->folio_count,
					size,
					buffer_index);
				return -EFAULT;
		}

		folio = from->folio_array[folio_index];
		folio_offset = from->folio_offsets[folio_index];
		folio_avail = folio_nr_pages(folio) * PAGE_SIZE - folio_offset;
		copy_amount = min(remaining, folio_avail);

		kaddr = kmap_local_folio(folio, folio_offset);
		copied = copy_to_iter(kaddr, copy_amount, iter);
		kunmap_local(kaddr);

		if (copied != copy_amount)
			return -EFAULT;

		remaining -= copied;
		folio_index++;
	}

	return 0;
}
