#undef TRACE_SYSTEM
#define TRACE_SYSTEM filemap

#if !defined(_TRACE_FILEMAP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_FILEMAP_H

#include <linux/types.h>
#include <linux/tracepoint.h>
#include <linux/mm.h>
#include <linux/memcontrol.h>
#include <linux/device.h>
#include <linux/kdev_t.h>

DECLARE_EVENT_CLASS(mm_filemap_op_page_cache,

	TP_PROTO(struct page *page),

	TP_ARGS(page),

	TP_STRUCT__entry(
		__field(struct page *, page)
		__field(unsigned long, i_ino)
		__field(unsigned long, index)
		__field(dev_t, s_dev)
	),

	TP_fast_assign(
		__entry->page = page;
		__entry->i_ino = page->mapping->host->i_ino;
		__entry->index = page->index;
		if (page->mapping->host->i_sb)
			__entry->s_dev = page->mapping->host->i_sb->s_dev;
		else
			__entry->s_dev = page->mapping->host->i_rdev;
	),

	TP_printk("dev %d:%d ino %lx page=%p pfn=%lu ofs=%lu",
		MAJOR(__entry->s_dev), MINOR(__entry->s_dev),
		__entry->i_ino,
		__entry->page,
		page_to_pfn(__entry->page),
		__entry->index << PAGE_SHIFT)
);

DEFINE_EVENT(mm_filemap_op_page_cache, mm_filemap_delete_from_page_cache,
	TP_PROTO(struct page *page),
	TP_ARGS(page)
	);

DEFINE_EVENT(mm_filemap_op_page_cache, mm_filemap_add_to_page_cache,
	TP_PROTO(struct page *page),
	TP_ARGS(page)
	);

DECLARE_EVENT_CLASS(mm_filemap_find_page_cache_miss,

	TP_PROTO(struct file *file, loff_t pos, size_t count, int read),

	TP_ARGS(file, pos, count, read),

	TP_STRUCT__entry(
		__array(char, path, MAX_FILTER_STR_VAL)
		__field(char *, path_name)
		__field(loff_t, pos)
		__field(size_t, count)
		__field(int, miss)
	),

	TP_fast_assign(
		__entry->path_name = d_path(&file->f_path, __entry->path, MAX_FILTER_STR_VAL);
		__entry->pos	= pos;
		__entry->count	= count;
		__entry->miss = 0;
		if ((pos & ~PAGE_CACHE_MASK) || (count % PAGE_SIZE) || read) {
			unsigned long ret;
			rcu_read_lock();
			ret = (count ? page_cache_next_hole(file->f_mapping,
					(pos >> PAGE_CACHE_SHIFT), ((count - 1) >> PAGE_CACHE_SHIFT) + 1) : 0);
			rcu_read_unlock();
			__entry->miss = (ret >= (pos >> PAGE_CACHE_SHIFT) &&
					ret <= ((pos + count - 1) >> PAGE_CACHE_SHIFT));
		}
	),

	TP_printk("path_name %s pos %lld count %lu miss %s",
		  __entry->path_name,
		  __entry->pos, __entry->count,
		  (__entry->miss ? "yes" : "no"))
);

DEFINE_EVENT(mm_filemap_find_page_cache_miss, mm_filemap_do_generic_file_read,
	TP_PROTO(struct file *file, loff_t pos, size_t count, int read),
	TP_ARGS(file, pos, count, read)
	);

DEFINE_EVENT(mm_filemap_find_page_cache_miss, mm_filemap_generic_perform_write,
	TP_PROTO(struct file *file, loff_t pos, size_t count, int read),
	TP_ARGS(file, pos, count, read)
	);

#endif /* _TRACE_FILEMAP_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
