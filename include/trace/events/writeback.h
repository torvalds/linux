#undef TRACE_SYSTEM
#define TRACE_SYSTEM writeback

#if !defined(_TRACE_WRITEBACK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_WRITEBACK_H

#include <linux/backing-dev.h>
#include <linux/device.h>
#include <linux/writeback.h>

struct wb_writeback_work;

DECLARE_EVENT_CLASS(writeback_work_class,
	TP_PROTO(struct backing_dev_info *bdi, struct wb_writeback_work *work),
	TP_ARGS(bdi, work),
	TP_STRUCT__entry(
		__array(char, name, 32)
		__field(long, nr_pages)
		__field(dev_t, sb_dev)
		__field(int, sync_mode)
		__field(int, for_kupdate)
		__field(int, range_cyclic)
		__field(int, for_background)
	),
	TP_fast_assign(
		strncpy(__entry->name, dev_name(bdi->dev), 32);
		__entry->nr_pages = work->nr_pages;
		__entry->sb_dev = work->sb ? work->sb->s_dev : 0;
		__entry->sync_mode = work->sync_mode;
		__entry->for_kupdate = work->for_kupdate;
		__entry->range_cyclic = work->range_cyclic;
		__entry->for_background	= work->for_background;
	),
	TP_printk("bdi %s: sb_dev %d:%d nr_pages=%ld sync_mode=%d "
		  "kupdate=%d range_cyclic=%d background=%d",
		  __entry->name,
		  MAJOR(__entry->sb_dev), MINOR(__entry->sb_dev),
		  __entry->nr_pages,
		  __entry->sync_mode,
		  __entry->for_kupdate,
		  __entry->range_cyclic,
		  __entry->for_background
	)
);
#define DEFINE_WRITEBACK_WORK_EVENT(name) \
DEFINE_EVENT(writeback_work_class, name, \
	TP_PROTO(struct backing_dev_info *bdi, struct wb_writeback_work *work), \
	TP_ARGS(bdi, work))
DEFINE_WRITEBACK_WORK_EVENT(writeback_nothread);
DEFINE_WRITEBACK_WORK_EVENT(writeback_queue);
DEFINE_WRITEBACK_WORK_EVENT(writeback_exec);

TRACE_EVENT(writeback_pages_written,
	TP_PROTO(long pages_written),
	TP_ARGS(pages_written),
	TP_STRUCT__entry(
		__field(long,		pages)
	),
	TP_fast_assign(
		__entry->pages		= pages_written;
	),
	TP_printk("%ld", __entry->pages)
);

DECLARE_EVENT_CLASS(writeback_class,
	TP_PROTO(struct backing_dev_info *bdi),
	TP_ARGS(bdi),
	TP_STRUCT__entry(
		__array(char, name, 32)
	),
	TP_fast_assign(
		strncpy(__entry->name, dev_name(bdi->dev), 32);
	),
	TP_printk("bdi %s",
		  __entry->name
	)
);
#define DEFINE_WRITEBACK_EVENT(name) \
DEFINE_EVENT(writeback_class, name, \
	TP_PROTO(struct backing_dev_info *bdi), \
	TP_ARGS(bdi))

DEFINE_WRITEBACK_EVENT(writeback_nowork);
DEFINE_WRITEBACK_EVENT(writeback_wake_thread);
DEFINE_WRITEBACK_EVENT(writeback_wake_forker_thread);
DEFINE_WRITEBACK_EVENT(writeback_bdi_register);
DEFINE_WRITEBACK_EVENT(writeback_bdi_unregister);
DEFINE_WRITEBACK_EVENT(writeback_thread_start);
DEFINE_WRITEBACK_EVENT(writeback_thread_stop);

DECLARE_EVENT_CLASS(wbc_class,
	TP_PROTO(struct writeback_control *wbc, struct backing_dev_info *bdi),
	TP_ARGS(wbc, bdi),
	TP_STRUCT__entry(
		__array(char, name, 32)
		__field(long, nr_to_write)
		__field(long, pages_skipped)
		__field(int, sync_mode)
		__field(int, nonblocking)
		__field(int, encountered_congestion)
		__field(int, for_kupdate)
		__field(int, for_background)
		__field(int, for_reclaim)
		__field(int, range_cyclic)
		__field(int, more_io)
		__field(unsigned long, older_than_this)
		__field(long, range_start)
		__field(long, range_end)
	),

	TP_fast_assign(
		strncpy(__entry->name, dev_name(bdi->dev), 32);
		__entry->nr_to_write	= wbc->nr_to_write;
		__entry->pages_skipped	= wbc->pages_skipped;
		__entry->sync_mode	= wbc->sync_mode;
		__entry->for_kupdate	= wbc->for_kupdate;
		__entry->for_background	= wbc->for_background;
		__entry->for_reclaim	= wbc->for_reclaim;
		__entry->range_cyclic	= wbc->range_cyclic;
		__entry->more_io	= wbc->more_io;
		__entry->older_than_this = wbc->older_than_this ?
						*wbc->older_than_this : 0;
		__entry->range_start	= (long)wbc->range_start;
		__entry->range_end	= (long)wbc->range_end;
	),

	TP_printk("bdi %s: towrt=%ld skip=%ld mode=%d kupd=%d "
		"bgrd=%d reclm=%d cyclic=%d more=%d older=0x%lx "
		"start=0x%lx end=0x%lx",
		__entry->name,
		__entry->nr_to_write,
		__entry->pages_skipped,
		__entry->sync_mode,
		__entry->for_kupdate,
		__entry->for_background,
		__entry->for_reclaim,
		__entry->range_cyclic,
		__entry->more_io,
		__entry->older_than_this,
		__entry->range_start,
		__entry->range_end)
)

#define DEFINE_WBC_EVENT(name) \
DEFINE_EVENT(wbc_class, name, \
	TP_PROTO(struct writeback_control *wbc, struct backing_dev_info *bdi), \
	TP_ARGS(wbc, bdi))
DEFINE_WBC_EVENT(wbc_writeback_start);
DEFINE_WBC_EVENT(wbc_writeback_written);
DEFINE_WBC_EVENT(wbc_writeback_wait);
DEFINE_WBC_EVENT(wbc_balance_dirty_start);
DEFINE_WBC_EVENT(wbc_balance_dirty_written);
DEFINE_WBC_EVENT(wbc_balance_dirty_wait);
DEFINE_WBC_EVENT(wbc_writepage);

#endif /* _TRACE_WRITEBACK_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
