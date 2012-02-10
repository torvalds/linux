#undef TRACE_SYSTEM
#define TRACE_SYSTEM writeback

#if !defined(_TRACE_WRITEBACK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_WRITEBACK_H

#include <linux/backing-dev.h>
#include <linux/device.h>
#include <linux/writeback.h>

#define show_inode_state(state)					\
	__print_flags(state, "|",				\
		{I_DIRTY_SYNC,		"I_DIRTY_SYNC"},	\
		{I_DIRTY_DATASYNC,	"I_DIRTY_DATASYNC"},	\
		{I_DIRTY_PAGES,		"I_DIRTY_PAGES"},	\
		{I_NEW,			"I_NEW"},		\
		{I_WILL_FREE,		"I_WILL_FREE"},		\
		{I_FREEING,		"I_FREEING"},		\
		{I_CLEAR,		"I_CLEAR"},		\
		{I_SYNC,		"I_SYNC"},		\
		{I_REFERENCED,		"I_REFERENCED"}		\
	)

#define WB_WORK_REASON							\
		{WB_REASON_BACKGROUND,		"background"},		\
		{WB_REASON_TRY_TO_FREE_PAGES,	"try_to_free_pages"},	\
		{WB_REASON_SYNC,		"sync"},		\
		{WB_REASON_PERIODIC,		"periodic"},		\
		{WB_REASON_LAPTOP_TIMER,	"laptop_timer"},	\
		{WB_REASON_FREE_MORE_MEM,	"free_more_memory"},	\
		{WB_REASON_FS_FREE_SPACE,	"fs_free_space"},	\
		{WB_REASON_FORKER_THREAD,	"forker_thread"}

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
		__field(int, reason)
	),
	TP_fast_assign(
		strncpy(__entry->name, dev_name(bdi->dev), 32);
		__entry->nr_pages = work->nr_pages;
		__entry->sb_dev = work->sb ? work->sb->s_dev : 0;
		__entry->sync_mode = work->sync_mode;
		__entry->for_kupdate = work->for_kupdate;
		__entry->range_cyclic = work->range_cyclic;
		__entry->for_background	= work->for_background;
		__entry->reason = work->reason;
	),
	TP_printk("bdi %s: sb_dev %d:%d nr_pages=%ld sync_mode=%d "
		  "kupdate=%d range_cyclic=%d background=%d reason=%s",
		  __entry->name,
		  MAJOR(__entry->sb_dev), MINOR(__entry->sb_dev),
		  __entry->nr_pages,
		  __entry->sync_mode,
		  __entry->for_kupdate,
		  __entry->range_cyclic,
		  __entry->for_background,
		  __print_symbolic(__entry->reason, WB_WORK_REASON)
	)
);
#define DEFINE_WRITEBACK_WORK_EVENT(name) \
DEFINE_EVENT(writeback_work_class, name, \
	TP_PROTO(struct backing_dev_info *bdi, struct wb_writeback_work *work), \
	TP_ARGS(bdi, work))
DEFINE_WRITEBACK_WORK_EVENT(writeback_nothread);
DEFINE_WRITEBACK_WORK_EVENT(writeback_queue);
DEFINE_WRITEBACK_WORK_EVENT(writeback_exec);
DEFINE_WRITEBACK_WORK_EVENT(writeback_start);
DEFINE_WRITEBACK_WORK_EVENT(writeback_written);
DEFINE_WRITEBACK_WORK_EVENT(writeback_wait);

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
DEFINE_WRITEBACK_EVENT(writeback_wake_background);
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
		__field(int, for_kupdate)
		__field(int, for_background)
		__field(int, for_reclaim)
		__field(int, range_cyclic)
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
		__entry->range_start	= (long)wbc->range_start;
		__entry->range_end	= (long)wbc->range_end;
	),

	TP_printk("bdi %s: towrt=%ld skip=%ld mode=%d kupd=%d "
		"bgrd=%d reclm=%d cyclic=%d "
		"start=0x%lx end=0x%lx",
		__entry->name,
		__entry->nr_to_write,
		__entry->pages_skipped,
		__entry->sync_mode,
		__entry->for_kupdate,
		__entry->for_background,
		__entry->for_reclaim,
		__entry->range_cyclic,
		__entry->range_start,
		__entry->range_end)
)

#define DEFINE_WBC_EVENT(name) \
DEFINE_EVENT(wbc_class, name, \
	TP_PROTO(struct writeback_control *wbc, struct backing_dev_info *bdi), \
	TP_ARGS(wbc, bdi))
DEFINE_WBC_EVENT(wbc_writepage);

TRACE_EVENT(writeback_queue_io,
	TP_PROTO(struct bdi_writeback *wb,
		 struct wb_writeback_work *work,
		 int moved),
	TP_ARGS(wb, work, moved),
	TP_STRUCT__entry(
		__array(char,		name, 32)
		__field(unsigned long,	older)
		__field(long,		age)
		__field(int,		moved)
		__field(int,		reason)
	),
	TP_fast_assign(
		unsigned long *older_than_this = work->older_than_this;
		strncpy(__entry->name, dev_name(wb->bdi->dev), 32);
		__entry->older	= older_than_this ?  *older_than_this : 0;
		__entry->age	= older_than_this ?
				  (jiffies - *older_than_this) * 1000 / HZ : -1;
		__entry->moved	= moved;
		__entry->reason	= work->reason;
	),
	TP_printk("bdi %s: older=%lu age=%ld enqueue=%d reason=%s",
		__entry->name,
		__entry->older,	/* older_than_this in jiffies */
		__entry->age,	/* older_than_this in relative milliseconds */
		__entry->moved,
		__print_symbolic(__entry->reason, WB_WORK_REASON)
	)
);

TRACE_EVENT(global_dirty_state,

	TP_PROTO(unsigned long background_thresh,
		 unsigned long dirty_thresh
	),

	TP_ARGS(background_thresh,
		dirty_thresh
	),

	TP_STRUCT__entry(
		__field(unsigned long,	nr_dirty)
		__field(unsigned long,	nr_writeback)
		__field(unsigned long,	nr_unstable)
		__field(unsigned long,	background_thresh)
		__field(unsigned long,	dirty_thresh)
		__field(unsigned long,	dirty_limit)
		__field(unsigned long,	nr_dirtied)
		__field(unsigned long,	nr_written)
	),

	TP_fast_assign(
		__entry->nr_dirty	= global_page_state(NR_FILE_DIRTY);
		__entry->nr_writeback	= global_page_state(NR_WRITEBACK);
		__entry->nr_unstable	= global_page_state(NR_UNSTABLE_NFS);
		__entry->nr_dirtied	= global_page_state(NR_DIRTIED);
		__entry->nr_written	= global_page_state(NR_WRITTEN);
		__entry->background_thresh = background_thresh;
		__entry->dirty_thresh	= dirty_thresh;
		__entry->dirty_limit = global_dirty_limit;
	),

	TP_printk("dirty=%lu writeback=%lu unstable=%lu "
		  "bg_thresh=%lu thresh=%lu limit=%lu "
		  "dirtied=%lu written=%lu",
		  __entry->nr_dirty,
		  __entry->nr_writeback,
		  __entry->nr_unstable,
		  __entry->background_thresh,
		  __entry->dirty_thresh,
		  __entry->dirty_limit,
		  __entry->nr_dirtied,
		  __entry->nr_written
	)
);

#define KBps(x)			((x) << (PAGE_SHIFT - 10))

TRACE_EVENT(bdi_dirty_ratelimit,

	TP_PROTO(struct backing_dev_info *bdi,
		 unsigned long dirty_rate,
		 unsigned long task_ratelimit),

	TP_ARGS(bdi, dirty_rate, task_ratelimit),

	TP_STRUCT__entry(
		__array(char,		bdi, 32)
		__field(unsigned long,	write_bw)
		__field(unsigned long,	avg_write_bw)
		__field(unsigned long,	dirty_rate)
		__field(unsigned long,	dirty_ratelimit)
		__field(unsigned long,	task_ratelimit)
		__field(unsigned long,	balanced_dirty_ratelimit)
	),

	TP_fast_assign(
		strlcpy(__entry->bdi, dev_name(bdi->dev), 32);
		__entry->write_bw	= KBps(bdi->write_bandwidth);
		__entry->avg_write_bw	= KBps(bdi->avg_write_bandwidth);
		__entry->dirty_rate	= KBps(dirty_rate);
		__entry->dirty_ratelimit = KBps(bdi->dirty_ratelimit);
		__entry->task_ratelimit	= KBps(task_ratelimit);
		__entry->balanced_dirty_ratelimit =
					  KBps(bdi->balanced_dirty_ratelimit);
	),

	TP_printk("bdi %s: "
		  "write_bw=%lu awrite_bw=%lu dirty_rate=%lu "
		  "dirty_ratelimit=%lu task_ratelimit=%lu "
		  "balanced_dirty_ratelimit=%lu",
		  __entry->bdi,
		  __entry->write_bw,		/* write bandwidth */
		  __entry->avg_write_bw,	/* avg write bandwidth */
		  __entry->dirty_rate,		/* bdi dirty rate */
		  __entry->dirty_ratelimit,	/* base ratelimit */
		  __entry->task_ratelimit, /* ratelimit with position control */
		  __entry->balanced_dirty_ratelimit /* the balanced ratelimit */
	)
);

TRACE_EVENT(balance_dirty_pages,

	TP_PROTO(struct backing_dev_info *bdi,
		 unsigned long thresh,
		 unsigned long bg_thresh,
		 unsigned long dirty,
		 unsigned long bdi_thresh,
		 unsigned long bdi_dirty,
		 unsigned long dirty_ratelimit,
		 unsigned long task_ratelimit,
		 unsigned long dirtied,
		 unsigned long period,
		 long pause,
		 unsigned long start_time),

	TP_ARGS(bdi, thresh, bg_thresh, dirty, bdi_thresh, bdi_dirty,
		dirty_ratelimit, task_ratelimit,
		dirtied, period, pause, start_time),

	TP_STRUCT__entry(
		__array(	 char,	bdi, 32)
		__field(unsigned long,	limit)
		__field(unsigned long,	setpoint)
		__field(unsigned long,	dirty)
		__field(unsigned long,	bdi_setpoint)
		__field(unsigned long,	bdi_dirty)
		__field(unsigned long,	dirty_ratelimit)
		__field(unsigned long,	task_ratelimit)
		__field(unsigned int,	dirtied)
		__field(unsigned int,	dirtied_pause)
		__field(unsigned long,	paused)
		__field(	 long,	pause)
		__field(unsigned long,	period)
		__field(	 long,	think)
	),

	TP_fast_assign(
		unsigned long freerun = (thresh + bg_thresh) / 2;
		strlcpy(__entry->bdi, dev_name(bdi->dev), 32);

		__entry->limit		= global_dirty_limit;
		__entry->setpoint	= (global_dirty_limit + freerun) / 2;
		__entry->dirty		= dirty;
		__entry->bdi_setpoint	= __entry->setpoint *
						bdi_thresh / (thresh + 1);
		__entry->bdi_dirty	= bdi_dirty;
		__entry->dirty_ratelimit = KBps(dirty_ratelimit);
		__entry->task_ratelimit	= KBps(task_ratelimit);
		__entry->dirtied	= dirtied;
		__entry->dirtied_pause	= current->nr_dirtied_pause;
		__entry->think		= current->dirty_paused_when == 0 ? 0 :
			 (long)(jiffies - current->dirty_paused_when) * 1000/HZ;
		__entry->period		= period * 1000 / HZ;
		__entry->pause		= pause * 1000 / HZ;
		__entry->paused		= (jiffies - start_time) * 1000 / HZ;
	),


	TP_printk("bdi %s: "
		  "limit=%lu setpoint=%lu dirty=%lu "
		  "bdi_setpoint=%lu bdi_dirty=%lu "
		  "dirty_ratelimit=%lu task_ratelimit=%lu "
		  "dirtied=%u dirtied_pause=%u "
		  "paused=%lu pause=%ld period=%lu think=%ld",
		  __entry->bdi,
		  __entry->limit,
		  __entry->setpoint,
		  __entry->dirty,
		  __entry->bdi_setpoint,
		  __entry->bdi_dirty,
		  __entry->dirty_ratelimit,
		  __entry->task_ratelimit,
		  __entry->dirtied,
		  __entry->dirtied_pause,
		  __entry->paused,	/* ms */
		  __entry->pause,	/* ms */
		  __entry->period,	/* ms */
		  __entry->think	/* ms */
	  )
);

DECLARE_EVENT_CLASS(writeback_congest_waited_template,

	TP_PROTO(unsigned int usec_timeout, unsigned int usec_delayed),

	TP_ARGS(usec_timeout, usec_delayed),

	TP_STRUCT__entry(
		__field(	unsigned int,	usec_timeout	)
		__field(	unsigned int,	usec_delayed	)
	),

	TP_fast_assign(
		__entry->usec_timeout	= usec_timeout;
		__entry->usec_delayed	= usec_delayed;
	),

	TP_printk("usec_timeout=%u usec_delayed=%u",
			__entry->usec_timeout,
			__entry->usec_delayed)
);

DEFINE_EVENT(writeback_congest_waited_template, writeback_congestion_wait,

	TP_PROTO(unsigned int usec_timeout, unsigned int usec_delayed),

	TP_ARGS(usec_timeout, usec_delayed)
);

DEFINE_EVENT(writeback_congest_waited_template, writeback_wait_iff_congested,

	TP_PROTO(unsigned int usec_timeout, unsigned int usec_delayed),

	TP_ARGS(usec_timeout, usec_delayed)
);

DECLARE_EVENT_CLASS(writeback_single_inode_template,

	TP_PROTO(struct inode *inode,
		 struct writeback_control *wbc,
		 unsigned long nr_to_write
	),

	TP_ARGS(inode, wbc, nr_to_write),

	TP_STRUCT__entry(
		__array(char, name, 32)
		__field(unsigned long, ino)
		__field(unsigned long, state)
		__field(unsigned long, dirtied_when)
		__field(unsigned long, writeback_index)
		__field(long, nr_to_write)
		__field(unsigned long, wrote)
	),

	TP_fast_assign(
		strncpy(__entry->name,
			dev_name(inode->i_mapping->backing_dev_info->dev), 32);
		__entry->ino		= inode->i_ino;
		__entry->state		= inode->i_state;
		__entry->dirtied_when	= inode->dirtied_when;
		__entry->writeback_index = inode->i_mapping->writeback_index;
		__entry->nr_to_write	= nr_to_write;
		__entry->wrote		= nr_to_write - wbc->nr_to_write;
	),

	TP_printk("bdi %s: ino=%lu state=%s dirtied_when=%lu age=%lu "
		  "index=%lu to_write=%ld wrote=%lu",
		  __entry->name,
		  __entry->ino,
		  show_inode_state(__entry->state),
		  __entry->dirtied_when,
		  (jiffies - __entry->dirtied_when) / HZ,
		  __entry->writeback_index,
		  __entry->nr_to_write,
		  __entry->wrote
	)
);

DEFINE_EVENT(writeback_single_inode_template, writeback_single_inode_requeue,
	TP_PROTO(struct inode *inode,
		 struct writeback_control *wbc,
		 unsigned long nr_to_write),
	TP_ARGS(inode, wbc, nr_to_write)
);

DEFINE_EVENT(writeback_single_inode_template, writeback_single_inode,
	TP_PROTO(struct inode *inode,
		 struct writeback_control *wbc,
		 unsigned long nr_to_write),
	TP_ARGS(inode, wbc, nr_to_write)
);

#endif /* _TRACE_WRITEBACK_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
