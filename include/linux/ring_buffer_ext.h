/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_RING_BUFFER_EXT_H
#define _LINUX_RING_BUFFER_EXT_H
#include <linux/mm.h>
#include <linux/types.h>

struct rb_ext_stats {
	u64		entries;
	unsigned long	pages_touched;
	unsigned long	overrun;
};

#define RB_PAGE_FT_HEAD		(1 << 0)
#define RB_PAGE_FT_READER	(1 << 1)
#define RB_PAGE_FT_COMMIT	(1 << 2)

/*
 * The pages where the events are stored are the only shared elements between
 * the reader and the external writer. They are convenient to enable
 * communication from the writer to the reader. The data will be used by the
 * reader to update its view on the ring buffer.
 */
struct rb_ext_page_footer {
	atomic_t		writer_status;
	atomic_t		reader_status;
	struct rb_ext_stats	stats;
};

static inline struct rb_ext_page_footer *rb_ext_page_get_footer(void *page)
{
	struct rb_ext_page_footer *footer;
	unsigned long page_va = (unsigned long)page;

	page_va	= ALIGN_DOWN(page_va, PAGE_SIZE);

	return (struct rb_ext_page_footer *)(page_va + PAGE_SIZE -
					     sizeof(*footer));
}

#define BUF_EXT_PAGE_SIZE (BUF_PAGE_SIZE - sizeof(struct rb_ext_page_footer))

/*
 * An external writer can't rely on the internal struct ring_buffer_per_cpu.
 * Instead, allow to pack the relevant information into struct
 * ring_buffer_pack which can be sent to the writer. The latter can then create
 * its own view on the ring buffer.
 */
struct ring_buffer_pack {
	int		cpu;
	unsigned long	reader_page_va;
	unsigned long	nr_pages;
	unsigned long	page_va[];
};

struct trace_buffer_pack {
	int			nr_cpus;
	unsigned long		total_pages;
	char			__data[];	/* contains ring_buffer_pack */
};

static inline
struct ring_buffer_pack *__next_ring_buffer_pack(struct ring_buffer_pack *rb_pack)
{
	size_t len;

	len = offsetof(struct ring_buffer_pack, page_va) +
		       sizeof(unsigned long) * rb_pack->nr_pages;

	return (struct ring_buffer_pack *)((void *)rb_pack + len);
}

/*
 * Accessor for ring_buffer_pack's within trace_buffer_pack
 */
#define for_each_ring_buffer_pack(rb_pack, cpu, trace_pack)		\
	for (rb_pack = (struct ring_buffer_pack *)&trace_pack->__data[0], cpu = 0;	\
	     cpu < trace_pack->nr_cpus;					\
	     cpu++, rb_pack = __next_ring_buffer_pack(rb_pack))
#endif
