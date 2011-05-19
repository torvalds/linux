#ifndef _KERNEL_EVENTS_INTERNAL_H
#define _KERNEL_EVENTS_INTERNAL_H

#define RING_BUFFER_WRITABLE		0x01

struct ring_buffer {
	atomic_t			refcount;
	struct rcu_head			rcu_head;
#ifdef CONFIG_PERF_USE_VMALLOC
	struct work_struct		work;
	int				page_order;	/* allocation order  */
#endif
	int				nr_pages;	/* nr of data pages  */
	int				writable;	/* are we writable   */

	atomic_t			poll;		/* POLL_ for wakeups */

	local_t				head;		/* write position    */
	local_t				nest;		/* nested writers    */
	local_t				events;		/* event limit       */
	local_t				wakeup;		/* wakeup stamp      */
	local_t				lost;		/* nr records lost   */

	long				watermark;	/* wakeup watermark  */

	struct perf_event_mmap_page	*user_page;
	void				*data_pages[0];
};


extern void rb_free(struct ring_buffer *rb);
extern struct ring_buffer *
rb_alloc(int nr_pages, long watermark, int cpu, int flags);
extern void perf_event_wakeup(struct perf_event *event);

extern void
perf_event_header__init_id(struct perf_event_header *header,
			   struct perf_sample_data *data,
			   struct perf_event *event);
extern void
perf_event__output_id_sample(struct perf_event *event,
			     struct perf_output_handle *handle,
			     struct perf_sample_data *sample);

extern struct page *
perf_mmap_to_page(struct ring_buffer *rb, unsigned long pgoff);

#ifdef CONFIG_PERF_USE_VMALLOC
/*
 * Back perf_mmap() with vmalloc memory.
 *
 * Required for architectures that have d-cache aliasing issues.
 */

static inline int page_order(struct ring_buffer *rb)
{
	return rb->page_order;
}

#else

static inline int page_order(struct ring_buffer *rb)
{
	return 0;
}
#endif

static unsigned long perf_data_size(struct ring_buffer *rb)
{
	return rb->nr_pages << (PAGE_SHIFT + page_order(rb));
}

static inline void
__output_copy(struct perf_output_handle *handle,
		   const void *buf, unsigned int len)
{
	do {
		unsigned long size = min_t(unsigned long, handle->size, len);

		memcpy(handle->addr, buf, size);

		len -= size;
		handle->addr += size;
		buf += size;
		handle->size -= size;
		if (!handle->size) {
			struct ring_buffer *rb = handle->rb;

			handle->page++;
			handle->page &= rb->nr_pages - 1;
			handle->addr = rb->data_pages[handle->page];
			handle->size = PAGE_SIZE << page_order(rb);
		}
	} while (len);
}

#endif /* _KERNEL_EVENTS_INTERNAL_H */
