/*
 * Performance events ring-buffer code:
 *
 *  Copyright (C) 2008 Thomas Gleixner <tglx@linutronix.de>
 *  Copyright (C) 2008-2011 Red Hat, Inc., Ingo Molnar
 *  Copyright (C) 2008-2011 Red Hat, Inc., Peter Zijlstra <pzijlstr@redhat.com>
 *  Copyright  ©  2009 Paul Mackerras, IBM Corp. <paulus@au1.ibm.com>
 *
 * For licensing details see kernel-base/COPYING
 */

#include <linux/perf_event.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/circ_buf.h>
#include <linux/poll.h>

#include "internal.h"

static void perf_output_wakeup(struct perf_output_handle *handle)
{
	atomic_set(&handle->rb->poll, POLLIN);

	handle->event->pending_wakeup = 1;
	irq_work_queue(&handle->event->pending);
}

/*
 * We need to ensure a later event_id doesn't publish a head when a former
 * event isn't done writing. However since we need to deal with NMIs we
 * cannot fully serialize things.
 *
 * We only publish the head (and generate a wakeup) when the outer-most
 * event completes.
 */
static void perf_output_get_handle(struct perf_output_handle *handle)
{
	struct ring_buffer *rb = handle->rb;

	preempt_disable();
	local_inc(&rb->nest);
	handle->wakeup = local_read(&rb->wakeup);
}

static void perf_output_put_handle(struct perf_output_handle *handle)
{
	struct ring_buffer *rb = handle->rb;
	unsigned long head;

again:
	head = local_read(&rb->head);

	/*
	 * IRQ/NMI can happen here, which means we can miss a head update.
	 */

	if (!local_dec_and_test(&rb->nest))
		goto out;

	/*
	 * Since the mmap() consumer (userspace) can run on a different CPU:
	 *
	 *   kernel				user
	 *
	 *   if (LOAD ->data_tail) {		LOAD ->data_head
	 *			(A)		smp_rmb()	(C)
	 *	STORE $data			LOAD $data
	 *	smp_wmb()	(B)		smp_mb()	(D)
	 *	STORE ->data_head		STORE ->data_tail
	 *   }
	 *
	 * Where A pairs with D, and B pairs with C.
	 *
	 * In our case (A) is a control dependency that separates the load of
	 * the ->data_tail and the stores of $data. In case ->data_tail
	 * indicates there is no room in the buffer to store $data we do not.
	 *
	 * D needs to be a full barrier since it separates the data READ
	 * from the tail WRITE.
	 *
	 * For B a WMB is sufficient since it separates two WRITEs, and for C
	 * an RMB is sufficient since it separates two READs.
	 *
	 * See perf_output_begin().
	 */
	smp_wmb(); /* B, matches C */
	rb->user_page->data_head = head;

	/*
	 * Now check if we missed an update -- rely on previous implied
	 * compiler barriers to force a re-read.
	 */
	if (unlikely(head != local_read(&rb->head))) {
		local_inc(&rb->nest);
		goto again;
	}

	if (handle->wakeup != local_read(&rb->wakeup))
		perf_output_wakeup(handle);

out:
	preempt_enable();
}

int perf_output_begin(struct perf_output_handle *handle,
		      struct perf_event *event, unsigned int size)
{
	struct ring_buffer *rb;
	unsigned long tail, offset, head;
	int have_lost, page_shift;
	struct {
		struct perf_event_header header;
		u64			 id;
		u64			 lost;
	} lost_event;

	rcu_read_lock();
	/*
	 * For inherited events we send all the output towards the parent.
	 */
	if (event->parent)
		event = event->parent;

	rb = rcu_dereference(event->rb);
	if (unlikely(!rb))
		goto out;

	if (unlikely(!rb->nr_pages))
		goto out;

	handle->rb    = rb;
	handle->event = event;

	have_lost = local_read(&rb->lost);
	if (unlikely(have_lost)) {
		size += sizeof(lost_event);
		if (event->attr.sample_id_all)
			size += event->id_header_size;
	}

	perf_output_get_handle(handle);

	do {
		tail = READ_ONCE(rb->user_page->data_tail);
		offset = head = local_read(&rb->head);
		if (!rb->overwrite &&
		    unlikely(CIRC_SPACE(head, tail, perf_data_size(rb)) < size))
			goto fail;

		/*
		 * The above forms a control dependency barrier separating the
		 * @tail load above from the data stores below. Since the @tail
		 * load is required to compute the branch to fail below.
		 *
		 * A, matches D; the full memory barrier userspace SHOULD issue
		 * after reading the data and before storing the new tail
		 * position.
		 *
		 * See perf_output_put_handle().
		 */

		head += size;
	} while (local_cmpxchg(&rb->head, offset, head) != offset);

	/*
	 * We rely on the implied barrier() by local_cmpxchg() to ensure
	 * none of the data stores below can be lifted up by the compiler.
	 */

	if (unlikely(head - local_read(&rb->wakeup) > rb->watermark))
		local_add(rb->watermark, &rb->wakeup);

	page_shift = PAGE_SHIFT + page_order(rb);

	handle->page = (offset >> page_shift) & (rb->nr_pages - 1);
	offset &= (1UL << page_shift) - 1;
	handle->addr = rb->data_pages[handle->page] + offset;
	handle->size = (1UL << page_shift) - offset;

	if (unlikely(have_lost)) {
		struct perf_sample_data sample_data;

		lost_event.header.size = sizeof(lost_event);
		lost_event.header.type = PERF_RECORD_LOST;
		lost_event.header.misc = 0;
		lost_event.id          = event->id;
		lost_event.lost        = local_xchg(&rb->lost, 0);

		perf_event_header__init_id(&lost_event.header,
					   &sample_data, event);
		perf_output_put(handle, lost_event);
		perf_event__output_id_sample(event, handle, &sample_data);
	}

	return 0;

fail:
	local_inc(&rb->lost);
	perf_output_put_handle(handle);
out:
	rcu_read_unlock();

	return -ENOSPC;
}

unsigned int perf_output_copy(struct perf_output_handle *handle,
		      const void *buf, unsigned int len)
{
	return __output_copy(handle, buf, len);
}

unsigned int perf_output_skip(struct perf_output_handle *handle,
			      unsigned int len)
{
	return __output_skip(handle, NULL, len);
}

void perf_output_end(struct perf_output_handle *handle)
{
	perf_output_put_handle(handle);
	rcu_read_unlock();
}

static void rb_irq_work(struct irq_work *work);

static void
ring_buffer_init(struct ring_buffer *rb, long watermark, int flags)
{
	long max_size = perf_data_size(rb);

	if (watermark)
		rb->watermark = min(max_size, watermark);

	if (!rb->watermark)
		rb->watermark = max_size / 2;

	if (flags & RING_BUFFER_WRITABLE)
		rb->overwrite = 0;
	else
		rb->overwrite = 1;

	atomic_set(&rb->refcount, 1);

	INIT_LIST_HEAD(&rb->event_list);
	spin_lock_init(&rb->event_lock);
	init_irq_work(&rb->irq_work, rb_irq_work);
}

static void ring_buffer_put_async(struct ring_buffer *rb)
{
	if (!atomic_dec_and_test(&rb->refcount))
		return;

	rb->rcu_head.next = (void *)rb;
	irq_work_queue(&rb->irq_work);
}

/*
 * This is called before hardware starts writing to the AUX area to
 * obtain an output handle and make sure there's room in the buffer.
 * When the capture completes, call perf_aux_output_end() to commit
 * the recorded data to the buffer.
 *
 * The ordering is similar to that of perf_output_{begin,end}, with
 * the exception of (B), which should be taken care of by the pmu
 * driver, since ordering rules will differ depending on hardware.
 */
void *perf_aux_output_begin(struct perf_output_handle *handle,
			    struct perf_event *event)
{
	struct perf_event *output_event = event;
	unsigned long aux_head, aux_tail;
	struct ring_buffer *rb;

	if (output_event->parent)
		output_event = output_event->parent;

	/*
	 * Since this will typically be open across pmu::add/pmu::del, we
	 * grab ring_buffer's refcount instead of holding rcu read lock
	 * to make sure it doesn't disappear under us.
	 */
	rb = ring_buffer_get(output_event);
	if (!rb)
		return NULL;

	if (!rb_has_aux(rb) || !atomic_inc_not_zero(&rb->aux_refcount))
		goto err;

	/*
	 * Nesting is not supported for AUX area, make sure nested
	 * writers are caught early
	 */
	if (WARN_ON_ONCE(local_xchg(&rb->aux_nest, 1)))
		goto err_put;

	aux_head = local_read(&rb->aux_head);

	handle->rb = rb;
	handle->event = event;
	handle->head = aux_head;
	handle->size = 0;

	/*
	 * In overwrite mode, AUX data stores do not depend on aux_tail,
	 * therefore (A) control dependency barrier does not exist. The
	 * (B) <-> (C) ordering is still observed by the pmu driver.
	 */
	if (!rb->aux_overwrite) {
		aux_tail = ACCESS_ONCE(rb->user_page->aux_tail);
		handle->wakeup = local_read(&rb->aux_wakeup) + rb->aux_watermark;
		if (aux_head - aux_tail < perf_aux_size(rb))
			handle->size = CIRC_SPACE(aux_head, aux_tail, perf_aux_size(rb));

		/*
		 * handle->size computation depends on aux_tail load; this forms a
		 * control dependency barrier separating aux_tail load from aux data
		 * store that will be enabled on successful return
		 */
		if (!handle->size) { /* A, matches D */
			event->pending_disable = 1;
			perf_output_wakeup(handle);
			local_set(&rb->aux_nest, 0);
			goto err_put;
		}
	}

	return handle->rb->aux_priv;

err_put:
	rb_free_aux(rb);

err:
	ring_buffer_put_async(rb);
	handle->event = NULL;

	return NULL;
}

/*
 * Commit the data written by hardware into the ring buffer by adjusting
 * aux_head and posting a PERF_RECORD_AUX into the perf buffer. It is the
 * pmu driver's responsibility to observe ordering rules of the hardware,
 * so that all the data is externally visible before this is called.
 */
void perf_aux_output_end(struct perf_output_handle *handle, unsigned long size,
			 bool truncated)
{
	struct ring_buffer *rb = handle->rb;
	unsigned long aux_head;
	u64 flags = 0;

	if (truncated)
		flags |= PERF_AUX_FLAG_TRUNCATED;

	/* in overwrite mode, driver provides aux_head via handle */
	if (rb->aux_overwrite) {
		flags |= PERF_AUX_FLAG_OVERWRITE;

		aux_head = handle->head;
		local_set(&rb->aux_head, aux_head);
	} else {
		aux_head = local_read(&rb->aux_head);
		local_add(size, &rb->aux_head);
	}

	if (size || flags) {
		/*
		 * Only send RECORD_AUX if we have something useful to communicate
		 */

		perf_event_aux_event(handle->event, aux_head, size, flags);
	}

	aux_head = rb->user_page->aux_head = local_read(&rb->aux_head);

	if (aux_head - local_read(&rb->aux_wakeup) >= rb->aux_watermark) {
		perf_output_wakeup(handle);
		local_add(rb->aux_watermark, &rb->aux_wakeup);
	}
	handle->event = NULL;

	local_set(&rb->aux_nest, 0);
	rb_free_aux(rb);
	ring_buffer_put_async(rb);
}

/*
 * Skip over a given number of bytes in the AUX buffer, due to, for example,
 * hardware's alignment constraints.
 */
int perf_aux_output_skip(struct perf_output_handle *handle, unsigned long size)
{
	struct ring_buffer *rb = handle->rb;
	unsigned long aux_head;

	if (size > handle->size)
		return -ENOSPC;

	local_add(size, &rb->aux_head);

	aux_head = rb->user_page->aux_head = local_read(&rb->aux_head);
	if (aux_head - local_read(&rb->aux_wakeup) >= rb->aux_watermark) {
		perf_output_wakeup(handle);
		local_add(rb->aux_watermark, &rb->aux_wakeup);
		handle->wakeup = local_read(&rb->aux_wakeup) +
				 rb->aux_watermark;
	}

	handle->head = aux_head;
	handle->size -= size;

	return 0;
}

void *perf_get_aux(struct perf_output_handle *handle)
{
	/* this is only valid between perf_aux_output_begin and *_end */
	if (!handle->event)
		return NULL;

	return handle->rb->aux_priv;
}

#define PERF_AUX_GFP	(GFP_KERNEL | __GFP_ZERO | __GFP_NOWARN | __GFP_NORETRY)

static struct page *rb_alloc_aux_page(int node, int order)
{
	struct page *page;

	if (order > MAX_ORDER)
		order = MAX_ORDER;

	do {
		page = alloc_pages_node(node, PERF_AUX_GFP, order);
	} while (!page && order--);

	if (page && order) {
		/*
		 * Communicate the allocation size to the driver:
		 * if we managed to secure a high-order allocation,
		 * set its first page's private to this order;
		 * !PagePrivate(page) means it's just a normal page.
		 */
		split_page(page, order);
		SetPagePrivate(page);
		set_page_private(page, order);
	}

	return page;
}

static void rb_free_aux_page(struct ring_buffer *rb, int idx)
{
	struct page *page = virt_to_page(rb->aux_pages[idx]);

	ClearPagePrivate(page);
	page->mapping = NULL;
	__free_page(page);
}

int rb_alloc_aux(struct ring_buffer *rb, struct perf_event *event,
		 pgoff_t pgoff, int nr_pages, long watermark, int flags)
{
	bool overwrite = !(flags & RING_BUFFER_WRITABLE);
	int node = (event->cpu == -1) ? -1 : cpu_to_node(event->cpu);
	int ret = -ENOMEM, max_order = 0;

	if (!has_aux(event))
		return -ENOTSUPP;

	if (event->pmu->capabilities & PERF_PMU_CAP_AUX_NO_SG) {
		/*
		 * We need to start with the max_order that fits in nr_pages,
		 * not the other way around, hence ilog2() and not get_order.
		 */
		max_order = ilog2(nr_pages);

		/*
		 * PMU requests more than one contiguous chunks of memory
		 * for SW double buffering
		 */
		if ((event->pmu->capabilities & PERF_PMU_CAP_AUX_SW_DOUBLEBUF) &&
		    !overwrite) {
			if (!max_order)
				return -EINVAL;

			max_order--;
		}
	}

	rb->aux_pages = kzalloc_node(nr_pages * sizeof(void *), GFP_KERNEL, node);
	if (!rb->aux_pages)
		return -ENOMEM;

	rb->free_aux = event->pmu->free_aux;
	for (rb->aux_nr_pages = 0; rb->aux_nr_pages < nr_pages;) {
		struct page *page;
		int last, order;

		order = min(max_order, ilog2(nr_pages - rb->aux_nr_pages));
		page = rb_alloc_aux_page(node, order);
		if (!page)
			goto out;

		for (last = rb->aux_nr_pages + (1 << page_private(page));
		     last > rb->aux_nr_pages; rb->aux_nr_pages++)
			rb->aux_pages[rb->aux_nr_pages] = page_address(page++);
	}

	/*
	 * In overwrite mode, PMUs that don't support SG may not handle more
	 * than one contiguous allocation, since they rely on PMI to do double
	 * buffering. In this case, the entire buffer has to be one contiguous
	 * chunk.
	 */
	if ((event->pmu->capabilities & PERF_PMU_CAP_AUX_NO_SG) &&
	    overwrite) {
		struct page *page = virt_to_page(rb->aux_pages[0]);

		if (page_private(page) != max_order)
			goto out;
	}

	rb->aux_priv = event->pmu->setup_aux(event->cpu, rb->aux_pages, nr_pages,
					     overwrite);
	if (!rb->aux_priv)
		goto out;

	ret = 0;

	/*
	 * aux_pages (and pmu driver's private data, aux_priv) will be
	 * referenced in both producer's and consumer's contexts, thus
	 * we keep a refcount here to make sure either of the two can
	 * reference them safely.
	 */
	atomic_set(&rb->aux_refcount, 1);

	rb->aux_overwrite = overwrite;
	rb->aux_watermark = watermark;

	if (!rb->aux_watermark && !rb->aux_overwrite)
		rb->aux_watermark = nr_pages << (PAGE_SHIFT - 1);

out:
	if (!ret)
		rb->aux_pgoff = pgoff;
	else
		rb_free_aux(rb);

	return ret;
}

static void __rb_free_aux(struct ring_buffer *rb)
{
	int pg;

	if (rb->aux_priv) {
		rb->free_aux(rb->aux_priv);
		rb->free_aux = NULL;
		rb->aux_priv = NULL;
	}

	if (rb->aux_nr_pages) {
		for (pg = 0; pg < rb->aux_nr_pages; pg++)
			rb_free_aux_page(rb, pg);

		kfree(rb->aux_pages);
		rb->aux_nr_pages = 0;
	}
}

void rb_free_aux(struct ring_buffer *rb)
{
	if (atomic_dec_and_test(&rb->aux_refcount))
		irq_work_queue(&rb->irq_work);
}

static void rb_irq_work(struct irq_work *work)
{
	struct ring_buffer *rb = container_of(work, struct ring_buffer, irq_work);

	if (!atomic_read(&rb->aux_refcount))
		__rb_free_aux(rb);

	if (rb->rcu_head.next == (void *)rb)
		call_rcu(&rb->rcu_head, rb_free_rcu);
}

#ifndef CONFIG_PERF_USE_VMALLOC

/*
 * Back perf_mmap() with regular GFP_KERNEL-0 pages.
 */

static struct page *
__perf_mmap_to_page(struct ring_buffer *rb, unsigned long pgoff)
{
	if (pgoff > rb->nr_pages)
		return NULL;

	if (pgoff == 0)
		return virt_to_page(rb->user_page);

	return virt_to_page(rb->data_pages[pgoff - 1]);
}

static void *perf_mmap_alloc_page(int cpu)
{
	struct page *page;
	int node;

	node = (cpu == -1) ? cpu : cpu_to_node(cpu);
	page = alloc_pages_node(node, GFP_KERNEL | __GFP_ZERO, 0);
	if (!page)
		return NULL;

	return page_address(page);
}

struct ring_buffer *rb_alloc(int nr_pages, long watermark, int cpu, int flags)
{
	struct ring_buffer *rb;
	unsigned long size;
	int i;

	size = sizeof(struct ring_buffer);
	size += nr_pages * sizeof(void *);

	rb = kzalloc(size, GFP_KERNEL);
	if (!rb)
		goto fail;

	rb->user_page = perf_mmap_alloc_page(cpu);
	if (!rb->user_page)
		goto fail_user_page;

	for (i = 0; i < nr_pages; i++) {
		rb->data_pages[i] = perf_mmap_alloc_page(cpu);
		if (!rb->data_pages[i])
			goto fail_data_pages;
	}

	rb->nr_pages = nr_pages;

	ring_buffer_init(rb, watermark, flags);

	return rb;

fail_data_pages:
	for (i--; i >= 0; i--)
		free_page((unsigned long)rb->data_pages[i]);

	free_page((unsigned long)rb->user_page);

fail_user_page:
	kfree(rb);

fail:
	return NULL;
}

static void perf_mmap_free_page(unsigned long addr)
{
	struct page *page = virt_to_page((void *)addr);

	page->mapping = NULL;
	__free_page(page);
}

void rb_free(struct ring_buffer *rb)
{
	int i;

	perf_mmap_free_page((unsigned long)rb->user_page);
	for (i = 0; i < rb->nr_pages; i++)
		perf_mmap_free_page((unsigned long)rb->data_pages[i]);
	kfree(rb);
}

#else
static int data_page_nr(struct ring_buffer *rb)
{
	return rb->nr_pages << page_order(rb);
}

static struct page *
__perf_mmap_to_page(struct ring_buffer *rb, unsigned long pgoff)
{
	/* The '>' counts in the user page. */
	if (pgoff > data_page_nr(rb))
		return NULL;

	return vmalloc_to_page((void *)rb->user_page + pgoff * PAGE_SIZE);
}

static void perf_mmap_unmark_page(void *addr)
{
	struct page *page = vmalloc_to_page(addr);

	page->mapping = NULL;
}

static void rb_free_work(struct work_struct *work)
{
	struct ring_buffer *rb;
	void *base;
	int i, nr;

	rb = container_of(work, struct ring_buffer, work);
	nr = data_page_nr(rb);

	base = rb->user_page;
	/* The '<=' counts in the user page. */
	for (i = 0; i <= nr; i++)
		perf_mmap_unmark_page(base + (i * PAGE_SIZE));

	vfree(base);
	kfree(rb);
}

void rb_free(struct ring_buffer *rb)
{
	schedule_work(&rb->work);
}

struct ring_buffer *rb_alloc(int nr_pages, long watermark, int cpu, int flags)
{
	struct ring_buffer *rb;
	unsigned long size;
	void *all_buf;

	size = sizeof(struct ring_buffer);
	size += sizeof(void *);

	rb = kzalloc(size, GFP_KERNEL);
	if (!rb)
		goto fail;

	INIT_WORK(&rb->work, rb_free_work);

	all_buf = vmalloc_user((nr_pages + 1) * PAGE_SIZE);
	if (!all_buf)
		goto fail_all_buf;

	rb->user_page = all_buf;
	rb->data_pages[0] = all_buf + PAGE_SIZE;
	rb->page_order = ilog2(nr_pages);
	rb->nr_pages = !!nr_pages;

	ring_buffer_init(rb, watermark, flags);

	return rb;

fail_all_buf:
	kfree(rb);

fail:
	return NULL;
}

#endif

struct page *
perf_mmap_to_page(struct ring_buffer *rb, unsigned long pgoff)
{
	if (rb->aux_nr_pages) {
		/* above AUX space */
		if (pgoff > rb->aux_pgoff + rb->aux_nr_pages)
			return NULL;

		/* AUX space */
		if (pgoff >= rb->aux_pgoff)
			return virt_to_page(rb->aux_pages[pgoff - rb->aux_pgoff]);
	}

	return __perf_mmap_to_page(rb, pgoff);
}
