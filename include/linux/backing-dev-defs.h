#ifndef __LINUX_BACKING_DEV_DEFS_H
#define __LINUX_BACKING_DEV_DEFS_H

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/percpu_counter.h>
#include <linux/flex_proportions.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

struct page;
struct device;
struct dentry;

/*
 * Bits in bdi_writeback.state
 */
enum wb_state {
	WB_async_congested,	/* The async (write) queue is getting full */
	WB_sync_congested,	/* The sync queue is getting full */
	WB_registered,		/* bdi_register() was done */
	WB_writeback_running,	/* Writeback is in progress */
};

typedef int (congested_fn)(void *, int);

enum wb_stat_item {
	WB_RECLAIMABLE,
	WB_WRITEBACK,
	WB_DIRTIED,
	WB_WRITTEN,
	NR_WB_STAT_ITEMS
};

#define WB_STAT_BATCH (8*(1+ilog2(nr_cpu_ids)))

struct bdi_writeback {
	struct backing_dev_info *bdi;	/* our parent bdi */

	unsigned long state;		/* Always use atomic bitops on this */
	unsigned long last_old_flush;	/* last old data flush */

	struct list_head b_dirty;	/* dirty inodes */
	struct list_head b_io;		/* parked for writeback */
	struct list_head b_more_io;	/* parked for more writeback */
	struct list_head b_dirty_time;	/* time stamps are dirty */
	spinlock_t list_lock;		/* protects the b_* lists */

	struct percpu_counter stat[NR_WB_STAT_ITEMS];

	unsigned long bw_time_stamp;	/* last time write bw is updated */
	unsigned long dirtied_stamp;
	unsigned long written_stamp;	/* pages written at bw_time_stamp */
	unsigned long write_bandwidth;	/* the estimated write bandwidth */
	unsigned long avg_write_bandwidth; /* further smoothed write bw */

	/*
	 * The base dirty throttle rate, re-calculated on every 200ms.
	 * All the bdi tasks' dirty rate will be curbed under it.
	 * @dirty_ratelimit tracks the estimated @balanced_dirty_ratelimit
	 * in small steps and is much more smooth/stable than the latter.
	 */
	unsigned long dirty_ratelimit;
	unsigned long balanced_dirty_ratelimit;

	struct fprop_local_percpu completions;
	int dirty_exceeded;

	spinlock_t work_lock;		/* protects work_list & dwork scheduling */
	struct list_head work_list;
	struct delayed_work dwork;	/* work item used for writeback */
};

struct backing_dev_info {
	struct list_head bdi_list;
	unsigned long ra_pages;	/* max readahead in PAGE_CACHE_SIZE units */
	unsigned int capabilities; /* Device capabilities */
	congested_fn *congested_fn; /* Function pointer if device is md/dm */
	void *congested_data;	/* Pointer to aux data for congested func */

	char *name;

	unsigned int min_ratio;
	unsigned int max_ratio, max_prop_frac;

	struct bdi_writeback wb;  /* default writeback info for this bdi */

	struct device *dev;

	struct timer_list laptop_mode_wb_timer;

#ifdef CONFIG_DEBUG_FS
	struct dentry *debug_dir;
	struct dentry *debug_stats;
#endif
};

enum {
	BLK_RW_ASYNC	= 0,
	BLK_RW_SYNC	= 1,
};

void clear_bdi_congested(struct backing_dev_info *bdi, int sync);
void set_bdi_congested(struct backing_dev_info *bdi, int sync);

#endif	/* __LINUX_BACKING_DEV_DEFS_H */
