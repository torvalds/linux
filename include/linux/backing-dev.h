/*
 * include/linux/backing-dev.h
 *
 * low-level device information and state which is propagated up through
 * to high-level code.
 */

#ifndef _LINUX_BACKING_DEV_H
#define _LINUX_BACKING_DEV_H

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/writeback.h>
#include <linux/backing-dev-defs.h>

struct backing_dev_info *inode_to_bdi(struct inode *inode);

int __must_check bdi_init(struct backing_dev_info *bdi);
void bdi_destroy(struct backing_dev_info *bdi);

__printf(3, 4)
int bdi_register(struct backing_dev_info *bdi, struct device *parent,
		const char *fmt, ...);
int bdi_register_dev(struct backing_dev_info *bdi, dev_t dev);
void bdi_unregister(struct backing_dev_info *bdi);
int __must_check bdi_setup_and_register(struct backing_dev_info *, char *);
void bdi_start_writeback(struct backing_dev_info *bdi, long nr_pages,
			enum wb_reason reason);
void bdi_start_background_writeback(struct backing_dev_info *bdi);
void wb_workfn(struct work_struct *work);
int bdi_has_dirty_io(struct backing_dev_info *bdi);
void wb_wakeup_delayed(struct bdi_writeback *wb);

extern spinlock_t bdi_lock;
extern struct list_head bdi_list;

extern struct workqueue_struct *bdi_wq;

static inline int wb_has_dirty_io(struct bdi_writeback *wb)
{
	return !list_empty(&wb->b_dirty) ||
	       !list_empty(&wb->b_io) ||
	       !list_empty(&wb->b_more_io);
}

static inline void __add_wb_stat(struct bdi_writeback *wb,
				 enum wb_stat_item item, s64 amount)
{
	__percpu_counter_add(&wb->stat[item], amount, WB_STAT_BATCH);
}

static inline void __inc_wb_stat(struct bdi_writeback *wb,
				 enum wb_stat_item item)
{
	__add_wb_stat(wb, item, 1);
}

static inline void inc_wb_stat(struct bdi_writeback *wb, enum wb_stat_item item)
{
	unsigned long flags;

	local_irq_save(flags);
	__inc_wb_stat(wb, item);
	local_irq_restore(flags);
}

static inline void __dec_wb_stat(struct bdi_writeback *wb,
				 enum wb_stat_item item)
{
	__add_wb_stat(wb, item, -1);
}

static inline void dec_wb_stat(struct bdi_writeback *wb, enum wb_stat_item item)
{
	unsigned long flags;

	local_irq_save(flags);
	__dec_wb_stat(wb, item);
	local_irq_restore(flags);
}

static inline s64 wb_stat(struct bdi_writeback *wb, enum wb_stat_item item)
{
	return percpu_counter_read_positive(&wb->stat[item]);
}

static inline s64 __wb_stat_sum(struct bdi_writeback *wb,
				enum wb_stat_item item)
{
	return percpu_counter_sum_positive(&wb->stat[item]);
}

static inline s64 wb_stat_sum(struct bdi_writeback *wb, enum wb_stat_item item)
{
	s64 sum;
	unsigned long flags;

	local_irq_save(flags);
	sum = __wb_stat_sum(wb, item);
	local_irq_restore(flags);

	return sum;
}

extern void wb_writeout_inc(struct bdi_writeback *wb);

/*
 * maximal error of a stat counter.
 */
static inline unsigned long wb_stat_error(struct bdi_writeback *wb)
{
#ifdef CONFIG_SMP
	return nr_cpu_ids * WB_STAT_BATCH;
#else
	return 1;
#endif
}

int bdi_set_min_ratio(struct backing_dev_info *bdi, unsigned int min_ratio);
int bdi_set_max_ratio(struct backing_dev_info *bdi, unsigned int max_ratio);

/*
 * Flags in backing_dev_info::capability
 *
 * The first three flags control whether dirty pages will contribute to the
 * VM's accounting and whether writepages() should be called for dirty pages
 * (something that would not, for example, be appropriate for ramfs)
 *
 * WARNING: these flags are closely related and should not normally be
 * used separately.  The BDI_CAP_NO_ACCT_AND_WRITEBACK combines these
 * three flags into a single convenience macro.
 *
 * BDI_CAP_NO_ACCT_DIRTY:  Dirty pages shouldn't contribute to accounting
 * BDI_CAP_NO_WRITEBACK:   Don't write pages back
 * BDI_CAP_NO_ACCT_WB:     Don't automatically account writeback pages
 * BDI_CAP_STRICTLIMIT:    Keep number of dirty pages below bdi threshold.
 */
#define BDI_CAP_NO_ACCT_DIRTY	0x00000001
#define BDI_CAP_NO_WRITEBACK	0x00000002
#define BDI_CAP_NO_ACCT_WB	0x00000004
#define BDI_CAP_STABLE_WRITES	0x00000008
#define BDI_CAP_STRICTLIMIT	0x00000010

#define BDI_CAP_NO_ACCT_AND_WRITEBACK \
	(BDI_CAP_NO_WRITEBACK | BDI_CAP_NO_ACCT_DIRTY | BDI_CAP_NO_ACCT_WB)

extern struct backing_dev_info noop_backing_dev_info;

int writeback_in_progress(struct backing_dev_info *bdi);

static inline int bdi_congested(struct backing_dev_info *bdi, int bdi_bits)
{
	if (bdi->congested_fn)
		return bdi->congested_fn(bdi->congested_data, bdi_bits);
	return (bdi->wb.state & bdi_bits);
}

static inline int bdi_read_congested(struct backing_dev_info *bdi)
{
	return bdi_congested(bdi, 1 << WB_sync_congested);
}

static inline int bdi_write_congested(struct backing_dev_info *bdi)
{
	return bdi_congested(bdi, 1 << WB_async_congested);
}

static inline int bdi_rw_congested(struct backing_dev_info *bdi)
{
	return bdi_congested(bdi, (1 << WB_sync_congested) |
				  (1 << WB_async_congested));
}

long congestion_wait(int sync, long timeout);
long wait_iff_congested(struct zone *zone, int sync, long timeout);
int pdflush_proc_obsolete(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp, loff_t *ppos);

static inline bool bdi_cap_stable_pages_required(struct backing_dev_info *bdi)
{
	return bdi->capabilities & BDI_CAP_STABLE_WRITES;
}

static inline bool bdi_cap_writeback_dirty(struct backing_dev_info *bdi)
{
	return !(bdi->capabilities & BDI_CAP_NO_WRITEBACK);
}

static inline bool bdi_cap_account_dirty(struct backing_dev_info *bdi)
{
	return !(bdi->capabilities & BDI_CAP_NO_ACCT_DIRTY);
}

static inline bool bdi_cap_account_writeback(struct backing_dev_info *bdi)
{
	/* Paranoia: BDI_CAP_NO_WRITEBACK implies BDI_CAP_NO_ACCT_WB */
	return !(bdi->capabilities & (BDI_CAP_NO_ACCT_WB |
				      BDI_CAP_NO_WRITEBACK));
}

static inline bool mapping_cap_writeback_dirty(struct address_space *mapping)
{
	return bdi_cap_writeback_dirty(inode_to_bdi(mapping->host));
}

static inline bool mapping_cap_account_dirty(struct address_space *mapping)
{
	return bdi_cap_account_dirty(inode_to_bdi(mapping->host));
}

static inline int bdi_sched_wait(void *word)
{
	schedule();
	return 0;
}

#endif		/* _LINUX_BACKING_DEV_H */
