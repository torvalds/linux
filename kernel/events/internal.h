/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KERNEL_EVENTS_INTERNAL_H
#define _KERNEL_EVENTS_INTERNAL_H

#include <linux/hardirq.h>
#include <linux/uaccess.h>
#include <linux/refcount.h>

/* Buffer handling */

#define RING_BUFFER_WRITABLE		0x01

struct perf_buffer {
	refcount_t			refcount;
	struct rcu_head			rcu_head;
#ifdef CONFIG_PERF_USE_VMALLOC
	struct work_struct		work;
	int				page_order;	/* allocation order  */
#endif
	int				nr_pages;	/* nr of data pages  */
	int				overwrite;	/* can overwrite itself */
	int				paused;		/* can write into ring buffer */

	atomic_t			poll;		/* POLL_ for wakeups */

	local_t				head;		/* write position    */
	unsigned int			nest;		/* nested writers    */
	local_t				events;		/* event limit       */
	local_t				wakeup;		/* wakeup stamp      */
	local_t				lost;		/* nr records lost   */

	long				watermark;	/* wakeup watermark  */
	long				aux_watermark;
	/* poll crap */
	spinlock_t			event_lock;
	struct list_head		event_list;

	atomic_t			mmap_count;
	unsigned long			mmap_locked;
	struct user_struct		*mmap_user;

	/* AUX area */
	long				aux_head;
	unsigned int			aux_nest;
	long				aux_wakeup;	/* last aux_watermark boundary crossed by aux_head */
	unsigned long			aux_pgoff;
	int				aux_nr_pages;
	int				aux_overwrite;
	atomic_t			aux_mmap_count;
	unsigned long			aux_mmap_locked;
	void				(*free_aux)(void *);
	refcount_t			aux_refcount;
	int				aux_in_sampling;
	void				**aux_pages;
	void				*aux_priv;

	struct perf_event_mmap_page	*user_page;
	void				*data_pages[];
};

extern void rb_free(struct perf_buffer *rb);

static inline void rb_free_rcu(struct rcu_head *rcu_head)
{
	struct perf_buffer *rb;

	rb = container_of(rcu_head, struct perf_buffer, rcu_head);
	rb_free(rb);
}

static inline void rb_toggle_paused(struct perf_buffer *rb, bool pause)
{
	if (!pause && rb->nr_pages)
		rb->paused = 0;
	else
		rb->paused = 1;
}

extern struct perf_buffer *
rb_alloc(int nr_pages, long watermark, int cpu, int flags);
extern void perf_event_wakeup(struct perf_event *event);
extern int rb_alloc_aux(struct perf_buffer *rb, struct perf_event *event,
			pgoff_t pgoff, int nr_pages, long watermark, int flags);
extern void rb_free_aux(struct perf_buffer *rb);
extern struct perf_buffer *ring_buffer_get(struct perf_event *event);
extern void ring_buffer_put(struct perf_buffer *rb);

static inline bool rb_has_aux(struct perf_buffer *rb)
{
	return !!rb->aux_nr_pages;
}

void perf_event_aux_event(struct perf_event *event, unsigned long head,
			  unsigned long size, u64 flags);

extern struct page *
perf_mmap_to_page(struct perf_buffer *rb, unsigned long pgoff);

#ifdef CONFIG_PERF_USE_VMALLOC
/*
 * Back perf_mmap() with vmalloc memory.
 *
 * Required for architectures that have d-cache aliasing issues.
 */

static inline int page_order(struct perf_buffer *rb)
{
	return rb->page_order;
}

#else

static inline int page_order(struct perf_buffer *rb)
{
	return 0;
}
#endif

static inline int data_page_nr(struct perf_buffer *rb)
{
	return rb->nr_pages << page_order(rb);
}

static inline unsigned long perf_data_size(struct perf_buffer *rb)
{
	return rb->nr_pages << (PAGE_SHIFT + page_order(rb));
}

static inline unsigned long perf_aux_size(struct perf_buffer *rb)
{
	return rb->aux_nr_pages << PAGE_SHIFT;
}

#define __DEFINE_OUTPUT_COPY_BODY(advance_buf, memcpy_func, ...)	\
{									\
	unsigned long size, written;					\
									\
	do {								\
		size    = min(handle->size, len);			\
		written = memcpy_func(__VA_ARGS__);			\
		written = size - written;				\
									\
		len -= written;						\
		handle->addr += written;				\
		if (advance_buf)					\
			buf += written;					\
		handle->size -= written;				\
		if (!handle->size) {					\
			struct perf_buffer *rb = handle->rb;	\
									\
			handle->page++;					\
			handle->page &= rb->nr_pages - 1;		\
			handle->addr = rb->data_pages[handle->page];	\
			handle->size = PAGE_SIZE << page_order(rb);	\
		}							\
	} while (len && written == size);				\
									\
	return len;							\
}

#define DEFINE_OUTPUT_COPY(func_name, memcpy_func)			\
static inline unsigned long						\
func_name(struct perf_output_handle *handle,				\
	  const void *buf, unsigned long len)				\
__DEFINE_OUTPUT_COPY_BODY(true, memcpy_func, handle->addr, buf, size)

static inline unsigned long
__output_custom(struct perf_output_handle *handle, perf_copy_f copy_func,
		const void *buf, unsigned long len)
{
	unsigned long orig_len = len;
	__DEFINE_OUTPUT_COPY_BODY(false, copy_func, handle->addr, buf,
				  orig_len - len, size)
}

static inline unsigned long
memcpy_common(void *dst, const void *src, unsigned long n)
{
	memcpy(dst, src, n);
	return 0;
}

DEFINE_OUTPUT_COPY(__output_copy, memcpy_common)

static inline unsigned long
memcpy_skip(void *dst, const void *src, unsigned long n)
{
	return 0;
}

DEFINE_OUTPUT_COPY(__output_skip, memcpy_skip)

#ifndef arch_perf_out_copy_user
#define arch_perf_out_copy_user arch_perf_out_copy_user

static inline unsigned long
arch_perf_out_copy_user(void *dst, const void *src, unsigned long n)
{
	unsigned long ret;

	pagefault_disable();
	ret = __copy_from_user_inatomic(dst, src, n);
	pagefault_enable();

	return ret;
}
#endif

DEFINE_OUTPUT_COPY(__output_copy_user, arch_perf_out_copy_user)

static inline int get_recursion_context(int *recursion)
{
	unsigned int pc = preempt_count();
	unsigned char rctx = 0;

	rctx += !!(pc & (NMI_MASK));
	rctx += !!(pc & (NMI_MASK | HARDIRQ_MASK));
	rctx += !!(pc & (NMI_MASK | HARDIRQ_MASK | SOFTIRQ_OFFSET));

	if (recursion[rctx])
		return -1;

	recursion[rctx]++;
	barrier();

	return rctx;
}

static inline void put_recursion_context(int *recursion, int rctx)
{
	barrier();
	recursion[rctx]--;
}

#ifdef CONFIG_HAVE_PERF_USER_STACK_DUMP
static inline bool arch_perf_have_user_stack_dump(void)
{
	return true;
}

#define perf_user_stack_pointer(regs) user_stack_pointer(regs)
#else
static inline bool arch_perf_have_user_stack_dump(void)
{
	return false;
}

#define perf_user_stack_pointer(regs) 0
#endif /* CONFIG_HAVE_PERF_USER_STACK_DUMP */

#endif /* _KERNEL_EVENTS_INTERNAL_H */
