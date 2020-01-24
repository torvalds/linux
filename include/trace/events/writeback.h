/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM writeback

#if !defined(_TRACE_WRITEBACK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_WRITEBACK_H

#include <linux/tracepoint.h>
#include <linux/backing-dev.h>
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
		{I_DIRTY_TIME,		"I_DIRTY_TIME"},	\
		{I_DIRTY_TIME_EXPIRED,	"I_DIRTY_TIME_EXPIRED"}, \
		{I_REFERENCED,		"I_REFERENCED"}		\
	)

/* enums need to be exported to user space */
#undef EM
#undef EMe
#define EM(a,b) 	TRACE_DEFINE_ENUM(a);
#define EMe(a,b)	TRACE_DEFINE_ENUM(a);

#define WB_WORK_REASON							\
	EM( WB_REASON_BACKGROUND,		"background")		\
	EM( WB_REASON_VMSCAN,			"vmscan")		\
	EM( WB_REASON_SYNC,			"sync")			\
	EM( WB_REASON_PERIODIC,			"periodic")		\
	EM( WB_REASON_LAPTOP_TIMER,		"laptop_timer")		\
	EM( WB_REASON_FREE_MORE_MEM,		"free_more_memory")	\
	EM( WB_REASON_FS_FREE_SPACE,		"fs_free_space")	\
	EMe(WB_REASON_FORKER_THREAD,		"forker_thread")

WB_WORK_REASON

/*
 * Now redefine the EM() and EMe() macros to map the enums to the strings
 * that will be printed in the output.
 */
#undef EM
#undef EMe
#define EM(a,b)		{ a, b },
#define EMe(a,b)	{ a, b }

struct wb_writeback_work;

DECLARE_EVENT_CLASS(writeback_page_template,

	TP_PROTO(struct page *page, struct address_space *mapping),

	TP_ARGS(page, mapping),

	TP_STRUCT__entry (
		__array(char, name, 32)
		__field(ino_t, ino)
		__field(pgoff_t, index)
	),

	TP_fast_assign(
		strscpy_pad(__entry->name,
			    mapping ? dev_name(inode_to_bdi(mapping->host)->dev) : "(unknown)",
			    32);
		__entry->ino = mapping ? mapping->host->i_ino : 0;
		__entry->index = page->index;
	),

	TP_printk("bdi %s: ino=%lu index=%lu",
		__entry->name,
		(unsigned long)__entry->ino,
		__entry->index
	)
);

DEFINE_EVENT(writeback_page_template, writeback_dirty_page,

	TP_PROTO(struct page *page, struct address_space *mapping),

	TP_ARGS(page, mapping)
);

DEFINE_EVENT(writeback_page_template, wait_on_page_writeback,

	TP_PROTO(struct page *page, struct address_space *mapping),

	TP_ARGS(page, mapping)
);

DECLARE_EVENT_CLASS(writeback_dirty_inode_template,

	TP_PROTO(struct inode *inode, int flags),

	TP_ARGS(inode, flags),

	TP_STRUCT__entry (
		__array(char, name, 32)
		__field(ino_t, ino)
		__field(unsigned long, state)
		__field(unsigned long, flags)
	),

	TP_fast_assign(
		struct backing_dev_info *bdi = inode_to_bdi(inode);

		/* may be called for files on pseudo FSes w/ unregistered bdi */
		strscpy_pad(__entry->name,
			    bdi->dev ? dev_name(bdi->dev) : "(unknown)", 32);
		__entry->ino		= inode->i_ino;
		__entry->state		= inode->i_state;
		__entry->flags		= flags;
	),

	TP_printk("bdi %s: ino=%lu state=%s flags=%s",
		__entry->name,
		(unsigned long)__entry->ino,
		show_inode_state(__entry->state),
		show_inode_state(__entry->flags)
	)
);

DEFINE_EVENT(writeback_dirty_inode_template, writeback_mark_inode_dirty,

	TP_PROTO(struct inode *inode, int flags),

	TP_ARGS(inode, flags)
);

DEFINE_EVENT(writeback_dirty_inode_template, writeback_dirty_inode_start,

	TP_PROTO(struct inode *inode, int flags),

	TP_ARGS(inode, flags)
);

DEFINE_EVENT(writeback_dirty_inode_template, writeback_dirty_inode,

	TP_PROTO(struct inode *inode, int flags),

	TP_ARGS(inode, flags)
);

#ifdef CREATE_TRACE_POINTS
#ifdef CONFIG_CGROUP_WRITEBACK

static inline ino_t __trace_wb_assign_cgroup(struct bdi_writeback *wb)
{
	return cgroup_ino(wb->memcg_css->cgroup);
}

static inline ino_t __trace_wbc_assign_cgroup(struct writeback_control *wbc)
{
	if (wbc->wb)
		return __trace_wb_assign_cgroup(wbc->wb);
	else
		return 1;
}
#else	/* CONFIG_CGROUP_WRITEBACK */

static inline ino_t __trace_wb_assign_cgroup(struct bdi_writeback *wb)
{
	return 1;
}

static inline ino_t __trace_wbc_assign_cgroup(struct writeback_control *wbc)
{
	return 1;
}

#endif	/* CONFIG_CGROUP_WRITEBACK */
#endif	/* CREATE_TRACE_POINTS */

#ifdef CONFIG_CGROUP_WRITEBACK
TRACE_EVENT(inode_foreign_history,

	TP_PROTO(struct inode *inode, struct writeback_control *wbc,
		 unsigned int history),

	TP_ARGS(inode, wbc, history),

	TP_STRUCT__entry(
		__array(char,		name, 32)
		__field(ino_t,		ino)
		__field(ino_t,		cgroup_ino)
		__field(unsigned int,	history)
	),

	TP_fast_assign(
		strncpy(__entry->name, dev_name(inode_to_bdi(inode)->dev), 32);
		__entry->ino		= inode->i_ino;
		__entry->cgroup_ino	= __trace_wbc_assign_cgroup(wbc);
		__entry->history	= history;
	),

	TP_printk("bdi %s: ino=%lu cgroup_ino=%lu history=0x%x",
		__entry->name,
		(unsigned long)__entry->ino,
		(unsigned long)__entry->cgroup_ino,
		__entry->history
	)
);

TRACE_EVENT(inode_switch_wbs,

	TP_PROTO(struct inode *inode, struct bdi_writeback *old_wb,
		 struct bdi_writeback *new_wb),

	TP_ARGS(inode, old_wb, new_wb),

	TP_STRUCT__entry(
		__array(char,		name, 32)
		__field(ino_t,		ino)
		__field(ino_t,		old_cgroup_ino)
		__field(ino_t,		new_cgroup_ino)
	),

	TP_fast_assign(
		strncpy(__entry->name,	dev_name(old_wb->bdi->dev), 32);
		__entry->ino		= inode->i_ino;
		__entry->old_cgroup_ino	= __trace_wb_assign_cgroup(old_wb);
		__entry->new_cgroup_ino	= __trace_wb_assign_cgroup(new_wb);
	),

	TP_printk("bdi %s: ino=%lu old_cgroup_ino=%lu new_cgroup_ino=%lu",
		__entry->name,
		(unsigned long)__entry->ino,
		(unsigned long)__entry->old_cgroup_ino,
		(unsigned long)__entry->new_cgroup_ino
	)
);

TRACE_EVENT(track_foreign_dirty,

	TP_PROTO(struct page *page, struct bdi_writeback *wb),

	TP_ARGS(page, wb),

	TP_STRUCT__entry(
		__array(char,		name, 32)
		__field(u64,		bdi_id)
		__field(ino_t,		ino)
		__field(unsigned int,	memcg_id)
		__field(ino_t,		cgroup_ino)
		__field(ino_t,		page_cgroup_ino)
	),

	TP_fast_assign(
		struct address_space *mapping = page_mapping(page);
		struct inode *inode = mapping ? mapping->host : NULL;

		strncpy(__entry->name,	dev_name(wb->bdi->dev), 32);
		__entry->bdi_id		= wb->bdi->id;
		__entry->ino		= inode ? inode->i_ino : 0;
		__entry->memcg_id	= wb->memcg_css->id;
		__entry->cgroup_ino	= __trace_wb_assign_cgroup(wb);
		__entry->page_cgroup_ino = cgroup_ino(page->mem_cgroup->css.cgroup);
	),

	TP_printk("bdi %s[%llu]: ino=%lu memcg_id=%u cgroup_ino=%lu page_cgroup_ino=%lu",
		__entry->name,
		__entry->bdi_id,
		(unsigned long)__entry->ino,
		__entry->memcg_id,
		(unsigned long)__entry->cgroup_ino,
		(unsigned long)__entry->page_cgroup_ino
	)
);

TRACE_EVENT(flush_foreign,

	TP_PROTO(struct bdi_writeback *wb, unsigned int frn_bdi_id,
		 unsigned int frn_memcg_id),

	TP_ARGS(wb, frn_bdi_id, frn_memcg_id),

	TP_STRUCT__entry(
		__array(char,		name, 32)
		__field(ino_t,		cgroup_ino)
		__field(unsigned int,	frn_bdi_id)
		__field(unsigned int,	frn_memcg_id)
	),

	TP_fast_assign(
		strncpy(__entry->name,	dev_name(wb->bdi->dev), 32);
		__entry->cgroup_ino	= __trace_wb_assign_cgroup(wb);
		__entry->frn_bdi_id	= frn_bdi_id;
		__entry->frn_memcg_id	= frn_memcg_id;
	),

	TP_printk("bdi %s: cgroup_ino=%lu frn_bdi_id=%u frn_memcg_id=%u",
		__entry->name,
		(unsigned long)__entry->cgroup_ino,
		__entry->frn_bdi_id,
		__entry->frn_memcg_id
	)
);
#endif

DECLARE_EVENT_CLASS(writeback_write_inode_template,

	TP_PROTO(struct inode *inode, struct writeback_control *wbc),

	TP_ARGS(inode, wbc),

	TP_STRUCT__entry (
		__array(char, name, 32)
		__field(ino_t, ino)
		__field(int, sync_mode)
		__field(ino_t, cgroup_ino)
	),

	TP_fast_assign(
		strscpy_pad(__entry->name,
			    dev_name(inode_to_bdi(inode)->dev), 32);
		__entry->ino		= inode->i_ino;
		__entry->sync_mode	= wbc->sync_mode;
		__entry->cgroup_ino	= __trace_wbc_assign_cgroup(wbc);
	),

	TP_printk("bdi %s: ino=%lu sync_mode=%d cgroup_ino=%lu",
		__entry->name,
		(unsigned long)__entry->ino,
		__entry->sync_mode,
		(unsigned long)__entry->cgroup_ino
	)
);

DEFINE_EVENT(writeback_write_inode_template, writeback_write_inode_start,

	TP_PROTO(struct inode *inode, struct writeback_control *wbc),

	TP_ARGS(inode, wbc)
);

DEFINE_EVENT(writeback_write_inode_template, writeback_write_inode,

	TP_PROTO(struct inode *inode, struct writeback_control *wbc),

	TP_ARGS(inode, wbc)
);

DECLARE_EVENT_CLASS(writeback_work_class,
	TP_PROTO(struct bdi_writeback *wb, struct wb_writeback_work *work),
	TP_ARGS(wb, work),
	TP_STRUCT__entry(
		__array(char, name, 32)
		__field(long, nr_pages)
		__field(dev_t, sb_dev)
		__field(int, sync_mode)
		__field(int, for_kupdate)
		__field(int, range_cyclic)
		__field(int, for_background)
		__field(int, reason)
		__field(ino_t, cgroup_ino)
	),
	TP_fast_assign(
		strscpy_pad(__entry->name,
			    wb->bdi->dev ? dev_name(wb->bdi->dev) :
			    "(unknown)", 32);
		__entry->nr_pages = work->nr_pages;
		__entry->sb_dev = work->sb ? work->sb->s_dev : 0;
		__entry->sync_mode = work->sync_mode;
		__entry->for_kupdate = work->for_kupdate;
		__entry->range_cyclic = work->range_cyclic;
		__entry->for_background	= work->for_background;
		__entry->reason = work->reason;
		__entry->cgroup_ino = __trace_wb_assign_cgroup(wb);
	),
	TP_printk("bdi %s: sb_dev %d:%d nr_pages=%ld sync_mode=%d "
		  "kupdate=%d range_cyclic=%d background=%d reason=%s cgroup_ino=%lu",
		  __entry->name,
		  MAJOR(__entry->sb_dev), MINOR(__entry->sb_dev),
		  __entry->nr_pages,
		  __entry->sync_mode,
		  __entry->for_kupdate,
		  __entry->range_cyclic,
		  __entry->for_background,
		  __print_symbolic(__entry->reason, WB_WORK_REASON),
		  (unsigned long)__entry->cgroup_ino
	)
);
#define DEFINE_WRITEBACK_WORK_EVENT(name) \
DEFINE_EVENT(writeback_work_class, name, \
	TP_PROTO(struct bdi_writeback *wb, struct wb_writeback_work *work), \
	TP_ARGS(wb, work))
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
	TP_PROTO(struct bdi_writeback *wb),
	TP_ARGS(wb),
	TP_STRUCT__entry(
		__array(char, name, 32)
		__field(ino_t, cgroup_ino)
	),
	TP_fast_assign(
		strscpy_pad(__entry->name, dev_name(wb->bdi->dev), 32);
		__entry->cgroup_ino = __trace_wb_assign_cgroup(wb);
	),
	TP_printk("bdi %s: cgroup_ino=%lu",
		  __entry->name,
		  (unsigned long)__entry->cgroup_ino
	)
);
#define DEFINE_WRITEBACK_EVENT(name) \
DEFINE_EVENT(writeback_class, name, \
	TP_PROTO(struct bdi_writeback *wb), \
	TP_ARGS(wb))

DEFINE_WRITEBACK_EVENT(writeback_wake_background);

TRACE_EVENT(writeback_bdi_register,
	TP_PROTO(struct backing_dev_info *bdi),
	TP_ARGS(bdi),
	TP_STRUCT__entry(
		__array(char, name, 32)
	),
	TP_fast_assign(
		strscpy_pad(__entry->name, dev_name(bdi->dev), 32);
	),
	TP_printk("bdi %s",
		__entry->name
	)
);

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
		__field(ino_t, cgroup_ino)
	),

	TP_fast_assign(
		strscpy_pad(__entry->name, dev_name(bdi->dev), 32);
		__entry->nr_to_write	= wbc->nr_to_write;
		__entry->pages_skipped	= wbc->pages_skipped;
		__entry->sync_mode	= wbc->sync_mode;
		__entry->for_kupdate	= wbc->for_kupdate;
		__entry->for_background	= wbc->for_background;
		__entry->for_reclaim	= wbc->for_reclaim;
		__entry->range_cyclic	= wbc->range_cyclic;
		__entry->range_start	= (long)wbc->range_start;
		__entry->range_end	= (long)wbc->range_end;
		__entry->cgroup_ino	= __trace_wbc_assign_cgroup(wbc);
	),

	TP_printk("bdi %s: towrt=%ld skip=%ld mode=%d kupd=%d "
		"bgrd=%d reclm=%d cyclic=%d "
		"start=0x%lx end=0x%lx cgroup_ino=%lu",
		__entry->name,
		__entry->nr_to_write,
		__entry->pages_skipped,
		__entry->sync_mode,
		__entry->for_kupdate,
		__entry->for_background,
		__entry->for_reclaim,
		__entry->range_cyclic,
		__entry->range_start,
		__entry->range_end,
		(unsigned long)__entry->cgroup_ino
	)
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
		__field(ino_t,		cgroup_ino)
	),
	TP_fast_assign(
		unsigned long *older_than_this = work->older_than_this;
		strscpy_pad(__entry->name, dev_name(wb->bdi->dev), 32);
		__entry->older	= older_than_this ?  *older_than_this : 0;
		__entry->age	= older_than_this ?
				  (jiffies - *older_than_this) * 1000 / HZ : -1;
		__entry->moved	= moved;
		__entry->reason	= work->reason;
		__entry->cgroup_ino	= __trace_wb_assign_cgroup(wb);
	),
	TP_printk("bdi %s: older=%lu age=%ld enqueue=%d reason=%s cgroup_ino=%lu",
		__entry->name,
		__entry->older,	/* older_than_this in jiffies */
		__entry->age,	/* older_than_this in relative milliseconds */
		__entry->moved,
		__print_symbolic(__entry->reason, WB_WORK_REASON),
		(unsigned long)__entry->cgroup_ino
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
		__entry->nr_dirty	= global_node_page_state(NR_FILE_DIRTY);
		__entry->nr_writeback	= global_node_page_state(NR_WRITEBACK);
		__entry->nr_unstable	= global_node_page_state(NR_UNSTABLE_NFS);
		__entry->nr_dirtied	= global_node_page_state(NR_DIRTIED);
		__entry->nr_written	= global_node_page_state(NR_WRITTEN);
		__entry->background_thresh = background_thresh;
		__entry->dirty_thresh	= dirty_thresh;
		__entry->dirty_limit	= global_wb_domain.dirty_limit;
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

	TP_PROTO(struct bdi_writeback *wb,
		 unsigned long dirty_rate,
		 unsigned long task_ratelimit),

	TP_ARGS(wb, dirty_rate, task_ratelimit),

	TP_STRUCT__entry(
		__array(char,		bdi, 32)
		__field(unsigned long,	write_bw)
		__field(unsigned long,	avg_write_bw)
		__field(unsigned long,	dirty_rate)
		__field(unsigned long,	dirty_ratelimit)
		__field(unsigned long,	task_ratelimit)
		__field(unsigned long,	balanced_dirty_ratelimit)
		__field(ino_t,		cgroup_ino)
	),

	TP_fast_assign(
		strscpy_pad(__entry->bdi, dev_name(wb->bdi->dev), 32);
		__entry->write_bw	= KBps(wb->write_bandwidth);
		__entry->avg_write_bw	= KBps(wb->avg_write_bandwidth);
		__entry->dirty_rate	= KBps(dirty_rate);
		__entry->dirty_ratelimit = KBps(wb->dirty_ratelimit);
		__entry->task_ratelimit	= KBps(task_ratelimit);
		__entry->balanced_dirty_ratelimit =
					KBps(wb->balanced_dirty_ratelimit);
		__entry->cgroup_ino	= __trace_wb_assign_cgroup(wb);
	),

	TP_printk("bdi %s: "
		  "write_bw=%lu awrite_bw=%lu dirty_rate=%lu "
		  "dirty_ratelimit=%lu task_ratelimit=%lu "
		  "balanced_dirty_ratelimit=%lu cgroup_ino=%lu",
		  __entry->bdi,
		  __entry->write_bw,		/* write bandwidth */
		  __entry->avg_write_bw,	/* avg write bandwidth */
		  __entry->dirty_rate,		/* bdi dirty rate */
		  __entry->dirty_ratelimit,	/* base ratelimit */
		  __entry->task_ratelimit, /* ratelimit with position control */
		  __entry->balanced_dirty_ratelimit, /* the balanced ratelimit */
		  (unsigned long)__entry->cgroup_ino
	)
);

TRACE_EVENT(balance_dirty_pages,

	TP_PROTO(struct bdi_writeback *wb,
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

	TP_ARGS(wb, thresh, bg_thresh, dirty, bdi_thresh, bdi_dirty,
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
		__field(ino_t,		cgroup_ino)
	),

	TP_fast_assign(
		unsigned long freerun = (thresh + bg_thresh) / 2;
		strscpy_pad(__entry->bdi, dev_name(wb->bdi->dev), 32);

		__entry->limit		= global_wb_domain.dirty_limit;
		__entry->setpoint	= (global_wb_domain.dirty_limit +
						freerun) / 2;
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
		__entry->cgroup_ino	= __trace_wb_assign_cgroup(wb);
	),


	TP_printk("bdi %s: "
		  "limit=%lu setpoint=%lu dirty=%lu "
		  "bdi_setpoint=%lu bdi_dirty=%lu "
		  "dirty_ratelimit=%lu task_ratelimit=%lu "
		  "dirtied=%u dirtied_pause=%u "
		  "paused=%lu pause=%ld period=%lu think=%ld cgroup_ino=%lu",
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
		  __entry->think,	/* ms */
		  (unsigned long)__entry->cgroup_ino
	  )
);

TRACE_EVENT(writeback_sb_inodes_requeue,

	TP_PROTO(struct inode *inode),
	TP_ARGS(inode),

	TP_STRUCT__entry(
		__array(char, name, 32)
		__field(ino_t, ino)
		__field(unsigned long, state)
		__field(unsigned long, dirtied_when)
		__field(ino_t, cgroup_ino)
	),

	TP_fast_assign(
		strscpy_pad(__entry->name,
			    dev_name(inode_to_bdi(inode)->dev), 32);
		__entry->ino		= inode->i_ino;
		__entry->state		= inode->i_state;
		__entry->dirtied_when	= inode->dirtied_when;
		__entry->cgroup_ino	= __trace_wb_assign_cgroup(inode_to_wb(inode));
	),

	TP_printk("bdi %s: ino=%lu state=%s dirtied_when=%lu age=%lu cgroup_ino=%lu",
		  __entry->name,
		  (unsigned long)__entry->ino,
		  show_inode_state(__entry->state),
		  __entry->dirtied_when,
		  (jiffies - __entry->dirtied_when) / HZ,
		  (unsigned long)__entry->cgroup_ino
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
		__field(ino_t, ino)
		__field(unsigned long, state)
		__field(unsigned long, dirtied_when)
		__field(unsigned long, writeback_index)
		__field(long, nr_to_write)
		__field(unsigned long, wrote)
		__field(ino_t, cgroup_ino)
	),

	TP_fast_assign(
		strscpy_pad(__entry->name,
			    dev_name(inode_to_bdi(inode)->dev), 32);
		__entry->ino		= inode->i_ino;
		__entry->state		= inode->i_state;
		__entry->dirtied_when	= inode->dirtied_when;
		__entry->writeback_index = inode->i_mapping->writeback_index;
		__entry->nr_to_write	= nr_to_write;
		__entry->wrote		= nr_to_write - wbc->nr_to_write;
		__entry->cgroup_ino	= __trace_wbc_assign_cgroup(wbc);
	),

	TP_printk("bdi %s: ino=%lu state=%s dirtied_when=%lu age=%lu "
		  "index=%lu to_write=%ld wrote=%lu cgroup_ino=%lu",
		  __entry->name,
		  (unsigned long)__entry->ino,
		  show_inode_state(__entry->state),
		  __entry->dirtied_when,
		  (jiffies - __entry->dirtied_when) / HZ,
		  __entry->writeback_index,
		  __entry->nr_to_write,
		  __entry->wrote,
		  (unsigned long)__entry->cgroup_ino
	)
);

DEFINE_EVENT(writeback_single_inode_template, writeback_single_inode_start,
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

DECLARE_EVENT_CLASS(writeback_inode_template,
	TP_PROTO(struct inode *inode),

	TP_ARGS(inode),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(unsigned long,	state			)
		__field(	__u16, mode			)
		__field(unsigned long, dirtied_when		)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->state	= inode->i_state;
		__entry->mode	= inode->i_mode;
		__entry->dirtied_when = inode->dirtied_when;
	),

	TP_printk("dev %d,%d ino %lu dirtied %lu state %s mode 0%o",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long)__entry->ino, __entry->dirtied_when,
		  show_inode_state(__entry->state), __entry->mode)
);

DEFINE_EVENT(writeback_inode_template, writeback_lazytime,
	TP_PROTO(struct inode *inode),

	TP_ARGS(inode)
);

DEFINE_EVENT(writeback_inode_template, writeback_lazytime_iput,
	TP_PROTO(struct inode *inode),

	TP_ARGS(inode)
);

DEFINE_EVENT(writeback_inode_template, writeback_dirty_inode_enqueue,

	TP_PROTO(struct inode *inode),

	TP_ARGS(inode)
);

/*
 * Inode writeback list tracking.
 */

DEFINE_EVENT(writeback_inode_template, sb_mark_inode_writeback,
	TP_PROTO(struct inode *inode),
	TP_ARGS(inode)
);

DEFINE_EVENT(writeback_inode_template, sb_clear_inode_writeback,
	TP_PROTO(struct inode *inode),
	TP_ARGS(inode)
);

#endif /* _TRACE_WRITEBACK_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
