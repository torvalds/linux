/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM readahead

#if !defined(_TRACE_FILEMAP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_READAHEAD_H

#include <linux/types.h>
#include <linux/tracepoint.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/pagemap.h>

TRACE_EVENT(page_cache_ra_unbounded,
	TP_PROTO(struct inode *inode, pgoff_t index, unsigned long nr_to_read,
		 unsigned long lookahead_size),

	TP_ARGS(inode, index, nr_to_read, lookahead_size),

	TP_STRUCT__entry(
		__field(unsigned long, i_ino)
		__field(dev_t, s_dev)
		__field(pgoff_t, index)
		__field(unsigned long, nr_to_read)
		__field(unsigned long, lookahead_size)
	),

	TP_fast_assign(
		__entry->i_ino = inode->i_ino;
		__entry->s_dev = inode->i_sb->s_dev;
		__entry->index = index;
		__entry->nr_to_read = nr_to_read;
		__entry->lookahead_size = lookahead_size;
	),

	TP_printk(
		"dev=%d:%d ino=%lx index=%lu nr_to_read=%lu lookahead_size=%lu",
		MAJOR(__entry->s_dev), MINOR(__entry->s_dev), __entry->i_ino,
		__entry->index, __entry->nr_to_read, __entry->lookahead_size
	)
);

TRACE_EVENT(page_cache_ra_order,
	TP_PROTO(struct inode *inode, pgoff_t index, struct file_ra_state *ra),

	TP_ARGS(inode, index, ra),

	TP_STRUCT__entry(
		__field(unsigned long, i_ino)
		__field(dev_t, s_dev)
		__field(pgoff_t, index)
		__field(unsigned int, order)
		__field(unsigned int, size)
		__field(unsigned int, async_size)
		__field(unsigned int, ra_pages)
	),

	TP_fast_assign(
		__entry->i_ino = inode->i_ino;
		__entry->s_dev = inode->i_sb->s_dev;
		__entry->index = index;
		__entry->order = ra->order;
		__entry->size = ra->size;
		__entry->async_size = ra->async_size;
		__entry->ra_pages = ra->ra_pages;
	),

	TP_printk(
		"dev=%d:%d ino=%lx index=%lu order=%u size=%u async_size=%u ra_pages=%u",
		MAJOR(__entry->s_dev), MINOR(__entry->s_dev), __entry->i_ino,
		__entry->index, __entry->order, __entry->size,
		__entry->async_size, __entry->ra_pages
	)
);

DECLARE_EVENT_CLASS(page_cache_ra_op,
	TP_PROTO(struct inode *inode, pgoff_t index, struct file_ra_state *ra,
		 unsigned long req_count),

	TP_ARGS(inode, index, ra, req_count),

	TP_STRUCT__entry(
		__field(unsigned long, i_ino)
		__field(dev_t, s_dev)
		__field(pgoff_t, index)
		__field(unsigned int, order)
		__field(unsigned int, size)
		__field(unsigned int, async_size)
		__field(unsigned int, ra_pages)
		__field(unsigned int, mmap_miss)
		__field(loff_t, prev_pos)
		__field(unsigned long, req_count)
	),

	TP_fast_assign(
		__entry->i_ino = inode->i_ino;
		__entry->s_dev = inode->i_sb->s_dev;
		__entry->index = index;
		__entry->order = ra->order;
		__entry->size = ra->size;
		__entry->async_size = ra->async_size;
		__entry->ra_pages = ra->ra_pages;
		__entry->mmap_miss = ra->mmap_miss;
		__entry->prev_pos = ra->prev_pos;
		__entry->req_count = req_count;
	),

	TP_printk(
		"dev=%d:%d ino=%lx index=%lu req_count=%lu order=%u size=%u async_size=%u ra_pages=%u mmap_miss=%u prev_pos=%lld",
		MAJOR(__entry->s_dev), MINOR(__entry->s_dev), __entry->i_ino,
		__entry->index, __entry->req_count, __entry->order,
		__entry->size, __entry->async_size, __entry->ra_pages,
		__entry->mmap_miss, __entry->prev_pos
	)
);

DEFINE_EVENT(page_cache_ra_op, page_cache_sync_ra,
	TP_PROTO(struct inode *inode, pgoff_t index, struct file_ra_state *ra,
		 unsigned long req_count),
	TP_ARGS(inode, index, ra, req_count)
);

DEFINE_EVENT(page_cache_ra_op, page_cache_async_ra,
	TP_PROTO(struct inode *inode, pgoff_t index, struct file_ra_state *ra,
		 unsigned long req_count),
	TP_ARGS(inode, index, ra, req_count)
);

#endif /* _TRACE_FILEMAP_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
