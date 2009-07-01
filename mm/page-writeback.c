/*
 * mm/page-writeback.c
 *
 * Copyright (C) 2002, Linus Torvalds.
 * Copyright (C) 2007 Red Hat, Inc., Peter Zijlstra <pzijlstr@redhat.com>
 *
 * Contains functions related to writing back dirty pages at the
 * address_space level.
 *
 * 10Apr2002	Andrew Morton
 *		Initial version
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <linux/init.h>
#include <linux/backing-dev.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/blkdev.h>
#include <linux/mpage.h>
#include <linux/rmap.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/smp.h>
#include <linux/sysctl.h>
#include <linux/cpu.h>
#include <linux/syscalls.h>
#include <linux/buffer_head.h>
#include <linux/pagevec.h>

/*
 * The maximum number of pages to writeout in a single bdflush/kupdate
 * operation.  We do this so we don't hold I_SYNC against an inode for
 * enormous amounts of time, which would block a userspace task which has
 * been forced to throttle against that inode.  Also, the code reevaluates
 * the dirty each time it has written this many pages.
 */
#define MAX_WRITEBACK_PAGES	1024

/*
 * After a CPU has dirtied this many pages, balance_dirty_pages_ratelimited
 * will look to see if it needs to force writeback or throttling.
 */
static long ratelimit_pages = 32;

/*
 * When balance_dirty_pages decides that the caller needs to perform some
 * non-background writeback, this is how many pages it will attempt to write.
 * It should be somewhat larger than RATELIMIT_PAGES to ensure that reasonably
 * large amounts of I/O are submitted.
 */
static inline long sync_writeback_pages(void)
{
	return ratelimit_pages + ratelimit_pages / 2;
}

/* The following parameters are exported via /proc/sys/vm */

/*
 * Start background writeback (via pdflush) at this percentage
 */
int dirty_background_ratio = 10;

/*
 * dirty_background_bytes starts at 0 (disabled) so that it is a function of
 * dirty_background_ratio * the amount of dirtyable memory
 */
unsigned long dirty_background_bytes;

/*
 * free highmem will not be subtracted from the total free memory
 * for calculating free ratios if vm_highmem_is_dirtyable is true
 */
int vm_highmem_is_dirtyable;

/*
 * The generator of dirty data starts writeback at this percentage
 */
int vm_dirty_ratio = 20;

/*
 * vm_dirty_bytes starts at 0 (disabled) so that it is a function of
 * vm_dirty_ratio * the amount of dirtyable memory
 */
unsigned long vm_dirty_bytes;

/*
 * The interval between `kupdate'-style writebacks
 */
unsigned int dirty_writeback_interval = 5 * 100; /* centiseconds */

/*
 * The longest time for which data is allowed to remain dirty
 */
unsigned int dirty_expire_interval = 30 * 100; /* centiseconds */

/*
 * Flag that makes the machine dump writes/reads and block dirtyings.
 */
int block_dump;

/*
 * Flag that puts the machine in "laptop mode". Doubles as a timeout in jiffies:
 * a full sync is triggered after this time elapses without any disk activity.
 */
int laptop_mode;

EXPORT_SYMBOL(laptop_mode);

/* End of sysctl-exported parameters */


static void background_writeout(unsigned long _min_pages);

/*
 * Scale the writeback cache size proportional to the relative writeout speeds.
 *
 * We do this by keeping a floating proportion between BDIs, based on page
 * writeback completions [end_page_writeback()]. Those devices that write out
 * pages fastest will get the larger share, while the slower will get a smaller
 * share.
 *
 * We use page writeout completions because we are interested in getting rid of
 * dirty pages. Having them written out is the primary goal.
 *
 * We introduce a concept of time, a period over which we measure these events,
 * because demand can/will vary over time. The length of this period itself is
 * measured in page writeback completions.
 *
 */
static struct prop_descriptor vm_completions;
static struct prop_descriptor vm_dirties;

/*
 * couple the period to the dirty_ratio:
 *
 *   period/2 ~ roundup_pow_of_two(dirty limit)
 */
static int calc_period_shift(void)
{
	unsigned long dirty_total;

	if (vm_dirty_bytes)
		dirty_total = vm_dirty_bytes / PAGE_SIZE;
	else
		dirty_total = (vm_dirty_ratio * determine_dirtyable_memory()) /
				100;
	return 2 + ilog2(dirty_total - 1);
}

/*
 * update the period when the dirty threshold changes.
 */
static void update_completion_period(void)
{
	int shift = calc_period_shift();
	prop_change_shift(&vm_completions, shift);
	prop_change_shift(&vm_dirties, shift);
}

int dirty_background_ratio_handler(struct ctl_table *table, int write,
		struct file *filp, void __user *buffer, size_t *lenp,
		loff_t *ppos)
{
	int ret;

	ret = proc_dointvec_minmax(table, write, filp, buffer, lenp, ppos);
	if (ret == 0 && write)
		dirty_background_bytes = 0;
	return ret;
}

int dirty_background_bytes_handler(struct ctl_table *table, int write,
		struct file *filp, void __user *buffer, size_t *lenp,
		loff_t *ppos)
{
	int ret;

	ret = proc_doulongvec_minmax(table, write, filp, buffer, lenp, ppos);
	if (ret == 0 && write)
		dirty_background_ratio = 0;
	return ret;
}

int dirty_ratio_handler(struct ctl_table *table, int write,
		struct file *filp, void __user *buffer, size_t *lenp,
		loff_t *ppos)
{
	int old_ratio = vm_dirty_ratio;
	int ret;

	ret = proc_dointvec_minmax(table, write, filp, buffer, lenp, ppos);
	if (ret == 0 && write && vm_dirty_ratio != old_ratio) {
		update_completion_period();
		vm_dirty_bytes = 0;
	}
	return ret;
}


int dirty_bytes_handler(struct ctl_table *table, int write,
		struct file *filp, void __user *buffer, size_t *lenp,
		loff_t *ppos)
{
	unsigned long old_bytes = vm_dirty_bytes;
	int ret;

	ret = proc_doulongvec_minmax(table, write, filp, buffer, lenp, ppos);
	if (ret == 0 && write && vm_dirty_bytes != old_bytes) {
		update_completion_period();
		vm_dirty_ratio = 0;
	}
	return ret;
}

/*
 * Increment the BDI's writeout completion count and the global writeout
 * completion count. Called from test_clear_page_writeback().
 */
static inline void __bdi_writeout_inc(struct backing_dev_info *bdi)
{
	__prop_inc_percpu_max(&vm_completions, &bdi->completions,
			      bdi->max_prop_frac);
}

void bdi_writeout_inc(struct backing_dev_info *bdi)
{
	unsigned long flags;

	local_irq_save(flags);
	__bdi_writeout_inc(bdi);
	local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(bdi_writeout_inc);

void task_dirty_inc(struct task_struct *tsk)
{
	prop_inc_single(&vm_dirties, &tsk->dirties);
}

/*
 * Obtain an accurate fraction of the BDI's portion.
 */
static void bdi_writeout_fraction(struct backing_dev_info *bdi,
		long *numerator, long *denominator)
{
	if (bdi_cap_writeback_dirty(bdi)) {
		prop_fraction_percpu(&vm_completions, &bdi->completions,
				numerator, denominator);
	} else {
		*numerator = 0;
		*denominator = 1;
	}
}

/*
 * Clip the earned share of dirty pages to that which is actually available.
 * This avoids exceeding the total dirty_limit when the floating averages
 * fluctuate too quickly.
 */
static void clip_bdi_dirty_limit(struct backing_dev_info *bdi,
		unsigned long dirty, unsigned long *pbdi_dirty)
{
	unsigned long avail_dirty;

	avail_dirty = global_page_state(NR_FILE_DIRTY) +
		 global_page_state(NR_WRITEBACK) +
		 global_page_state(NR_UNSTABLE_NFS) +
		 global_page_state(NR_WRITEBACK_TEMP);

	if (avail_dirty < dirty)
		avail_dirty = dirty - avail_dirty;
	else
		avail_dirty = 0;

	avail_dirty += bdi_stat(bdi, BDI_RECLAIMABLE) +
		bdi_stat(bdi, BDI_WRITEBACK);

	*pbdi_dirty = min(*pbdi_dirty, avail_dirty);
}

static inline void task_dirties_fraction(struct task_struct *tsk,
		long *numerator, long *denominator)
{
	prop_fraction_single(&vm_dirties, &tsk->dirties,
				numerator, denominator);
}

/*
 * scale the dirty limit
 *
 * task specific dirty limit:
 *
 *   dirty -= (dirty/8) * p_{t}
 */
static void task_dirty_limit(struct task_struct *tsk, unsigned long *pdirty)
{
	long numerator, denominator;
	unsigned long dirty = *pdirty;
	u64 inv = dirty >> 3;

	task_dirties_fraction(tsk, &numerator, &denominator);
	inv *= numerator;
	do_div(inv, denominator);

	dirty -= inv;
	if (dirty < *pdirty/2)
		dirty = *pdirty/2;

	*pdirty = dirty;
}

/*
 *
 */
static DEFINE_SPINLOCK(bdi_lock);
static unsigned int bdi_min_ratio;

int bdi_set_min_ratio(struct backing_dev_info *bdi, unsigned int min_ratio)
{
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&bdi_lock, flags);
	if (min_ratio > bdi->max_ratio) {
		ret = -EINVAL;
	} else {
		min_ratio -= bdi->min_ratio;
		if (bdi_min_ratio + min_ratio < 100) {
			bdi_min_ratio += min_ratio;
			bdi->min_ratio += min_ratio;
		} else {
			ret = -EINVAL;
		}
	}
	spin_unlock_irqrestore(&bdi_lock, flags);

	return ret;
}

int bdi_set_max_ratio(struct backing_dev_info *bdi, unsigned max_ratio)
{
	unsigned long flags;
	int ret = 0;

	if (max_ratio > 100)
		return -EINVAL;

	spin_lock_irqsave(&bdi_lock, flags);
	if (bdi->min_ratio > max_ratio) {
		ret = -EINVAL;
	} else {
		bdi->max_ratio = max_ratio;
		bdi->max_prop_frac = (PROP_FRAC_BASE * max_ratio) / 100;
	}
	spin_unlock_irqrestore(&bdi_lock, flags);

	return ret;
}
EXPORT_SYMBOL(bdi_set_max_ratio);

/*
 * Work out the current dirty-memory clamping and background writeout
 * thresholds.
 *
 * The main aim here is to lower them aggressively if there is a lot of mapped
 * memory around.  To avoid stressing page reclaim with lots of unreclaimable
 * pages.  It is better to clamp down on writers than to start swapping, and
 * performing lots of scanning.
 *
 * We only allow 1/2 of the currently-unmapped memory to be dirtied.
 *
 * We don't permit the clamping level to fall below 5% - that is getting rather
 * excessive.
 *
 * We make sure that the background writeout level is below the adjusted
 * clamping level.
 */

static unsigned long highmem_dirtyable_memory(unsigned long total)
{
#ifdef CONFIG_HIGHMEM
	int node;
	unsigned long x = 0;

	for_each_node_state(node, N_HIGH_MEMORY) {
		struct zone *z =
			&NODE_DATA(node)->node_zones[ZONE_HIGHMEM];

		x += zone_page_state(z, NR_FREE_PAGES) + zone_lru_pages(z);
	}
	/*
	 * Make sure that the number of highmem pages is never larger
	 * than the number of the total dirtyable memory. This can only
	 * occur in very strange VM situations but we want to make sure
	 * that this does not occur.
	 */
	return min(x, total);
#else
	return 0;
#endif
}

/**
 * determine_dirtyable_memory - amount of memory that may be used
 *
 * Returns the numebr of pages that can currently be freed and used
 * by the kernel for direct mappings.
 */
unsigned long determine_dirtyable_memory(void)
{
	unsigned long x;

	x = global_page_state(NR_FREE_PAGES) + global_lru_pages();

	if (!vm_highmem_is_dirtyable)
		x -= highmem_dirtyable_memory(x);

	return x + 1;	/* Ensure that we never return 0 */
}

void
get_dirty_limits(unsigned long *pbackground, unsigned long *pdirty,
		 unsigned long *pbdi_dirty, struct backing_dev_info *bdi)
{
	unsigned long background;
	unsigned long dirty;
	unsigned long available_memory = determine_dirtyable_memory();
	struct task_struct *tsk;

	if (vm_dirty_bytes)
		dirty = DIV_ROUND_UP(vm_dirty_bytes, PAGE_SIZE);
	else {
		int dirty_ratio;

		dirty_ratio = vm_dirty_ratio;
		if (dirty_ratio < 5)
			dirty_ratio = 5;
		dirty = (dirty_ratio * available_memory) / 100;
	}

	if (dirty_background_bytes)
		background = DIV_ROUND_UP(dirty_background_bytes, PAGE_SIZE);
	else
		background = (dirty_background_ratio * available_memory) / 100;

	if (background >= dirty)
		background = dirty / 2;
	tsk = current;
	if (tsk->flags & PF_LESS_THROTTLE || rt_task(tsk)) {
		background += background / 4;
		dirty += dirty / 4;
	}
	*pbackground = background;
	*pdirty = dirty;

	if (bdi) {
		u64 bdi_dirty;
		long numerator, denominator;

		/*
		 * Calculate this BDI's share of the dirty ratio.
		 */
		bdi_writeout_fraction(bdi, &numerator, &denominator);

		bdi_dirty = (dirty * (100 - bdi_min_ratio)) / 100;
		bdi_dirty *= numerator;
		do_div(bdi_dirty, denominator);
		bdi_dirty += (dirty * bdi->min_ratio) / 100;
		if (bdi_dirty > (dirty * bdi->max_ratio) / 100)
			bdi_dirty = dirty * bdi->max_ratio / 100;

		*pbdi_dirty = bdi_dirty;
		clip_bdi_dirty_limit(bdi, dirty, pbdi_dirty);
		task_dirty_limit(current, pbdi_dirty);
	}
}

/*
 * balance_dirty_pages() must be called by processes which are generating dirty
 * data.  It looks at the number of dirty pages in the machine and will force
 * the caller to perform writeback if the system is over `vm_dirty_ratio'.
 * If we're over `background_thresh' then pdflush is woken to perform some
 * writeout.
 */
static void balance_dirty_pages(struct address_space *mapping)
{
	long nr_reclaimable, bdi_nr_reclaimable;
	long nr_writeback, bdi_nr_writeback;
	unsigned long background_thresh;
	unsigned long dirty_thresh;
	unsigned long bdi_thresh;
	unsigned long pages_written = 0;
	unsigned long write_chunk = sync_writeback_pages();

	struct backing_dev_info *bdi = mapping->backing_dev_info;

	for (;;) {
		struct writeback_control wbc = {
			.bdi		= bdi,
			.sync_mode	= WB_SYNC_NONE,
			.older_than_this = NULL,
			.nr_to_write	= write_chunk,
			.range_cyclic	= 1,
		};

		get_dirty_limits(&background_thresh, &dirty_thresh,
				&bdi_thresh, bdi);

		nr_reclaimable = global_page_state(NR_FILE_DIRTY) +
					global_page_state(NR_UNSTABLE_NFS);
		nr_writeback = global_page_state(NR_WRITEBACK);

		bdi_nr_reclaimable = bdi_stat(bdi, BDI_RECLAIMABLE);
		bdi_nr_writeback = bdi_stat(bdi, BDI_WRITEBACK);

		if (bdi_nr_reclaimable + bdi_nr_writeback <= bdi_thresh)
			break;

		/*
		 * Throttle it only when the background writeback cannot
		 * catch-up. This avoids (excessively) small writeouts
		 * when the bdi limits are ramping up.
		 */
		if (nr_reclaimable + nr_writeback <
				(background_thresh + dirty_thresh) / 2)
			break;

		if (!bdi->dirty_exceeded)
			bdi->dirty_exceeded = 1;

		/* Note: nr_reclaimable denotes nr_dirty + nr_unstable.
		 * Unstable writes are a feature of certain networked
		 * filesystems (i.e. NFS) in which data may have been
		 * written to the server's write cache, but has not yet
		 * been flushed to permanent storage.
		 * Only move pages to writeback if this bdi is over its
		 * threshold otherwise wait until the disk writes catch
		 * up.
		 */
		if (bdi_nr_reclaimable > bdi_thresh) {
			writeback_inodes(&wbc);
			pages_written += write_chunk - wbc.nr_to_write;
			get_dirty_limits(&background_thresh, &dirty_thresh,
				       &bdi_thresh, bdi);
		}

		/*
		 * In order to avoid the stacked BDI deadlock we need
		 * to ensure we accurately count the 'dirty' pages when
		 * the threshold is low.
		 *
		 * Otherwise it would be possible to get thresh+n pages
		 * reported dirty, even though there are thresh-m pages
		 * actually dirty; with m+n sitting in the percpu
		 * deltas.
		 */
		if (bdi_thresh < 2*bdi_stat_error(bdi)) {
			bdi_nr_reclaimable = bdi_stat_sum(bdi, BDI_RECLAIMABLE);
			bdi_nr_writeback = bdi_stat_sum(bdi, BDI_WRITEBACK);
		} else if (bdi_nr_reclaimable) {
			bdi_nr_reclaimable = bdi_stat(bdi, BDI_RECLAIMABLE);
			bdi_nr_writeback = bdi_stat(bdi, BDI_WRITEBACK);
		}

		if (bdi_nr_reclaimable + bdi_nr_writeback <= bdi_thresh)
			break;
		if (pages_written >= write_chunk)
			break;		/* We've done our duty */

		congestion_wait(WRITE, HZ/10);
	}

	if (bdi_nr_reclaimable + bdi_nr_writeback < bdi_thresh &&
			bdi->dirty_exceeded)
		bdi->dirty_exceeded = 0;

	if (writeback_in_progress(bdi))
		return;		/* pdflush is already working this queue */

	/*
	 * In laptop mode, we wait until hitting the higher threshold before
	 * starting background writeout, and then write out all the way down
	 * to the lower threshold.  So slow writers cause minimal disk activity.
	 *
	 * In normal mode, we start background writeout at the lower
	 * background_thresh, to keep the amount of dirty memory low.
	 */
	if ((laptop_mode && pages_written) ||
			(!laptop_mode && (global_page_state(NR_FILE_DIRTY)
					  + global_page_state(NR_UNSTABLE_NFS)
					  > background_thresh)))
		pdflush_operation(background_writeout, 0);
}

void set_page_dirty_balance(struct page *page, int page_mkwrite)
{
	if (set_page_dirty(page) || page_mkwrite) {
		struct address_space *mapping = page_mapping(page);

		if (mapping)
			balance_dirty_pages_ratelimited(mapping);
	}
}

/**
 * balance_dirty_pages_ratelimited_nr - balance dirty memory state
 * @mapping: address_space which was dirtied
 * @nr_pages_dirtied: number of pages which the caller has just dirtied
 *
 * Processes which are dirtying memory should call in here once for each page
 * which was newly dirtied.  The function will periodically check the system's
 * dirty state and will initiate writeback if needed.
 *
 * On really big machines, get_writeback_state is expensive, so try to avoid
 * calling it too often (ratelimiting).  But once we're over the dirty memory
 * limit we decrease the ratelimiting by a lot, to prevent individual processes
 * from overshooting the limit by (ratelimit_pages) each.
 */
void balance_dirty_pages_ratelimited_nr(struct address_space *mapping,
					unsigned long nr_pages_dirtied)
{
	static DEFINE_PER_CPU(unsigned long, ratelimits) = 0;
	unsigned long ratelimit;
	unsigned long *p;

	ratelimit = ratelimit_pages;
	if (mapping->backing_dev_info->dirty_exceeded)
		ratelimit = 8;

	/*
	 * Check the rate limiting. Also, we do not want to throttle real-time
	 * tasks in balance_dirty_pages(). Period.
	 */
	preempt_disable();
	p =  &__get_cpu_var(ratelimits);
	*p += nr_pages_dirtied;
	if (unlikely(*p >= ratelimit)) {
		*p = 0;
		preempt_enable();
		balance_dirty_pages(mapping);
		return;
	}
	preempt_enable();
}
EXPORT_SYMBOL(balance_dirty_pages_ratelimited_nr);

void throttle_vm_writeout(gfp_t gfp_mask)
{
	unsigned long background_thresh;
	unsigned long dirty_thresh;

        for ( ; ; ) {
		get_dirty_limits(&background_thresh, &dirty_thresh, NULL, NULL);

                /*
                 * Boost the allowable dirty threshold a bit for page
                 * allocators so they don't get DoS'ed by heavy writers
                 */
                dirty_thresh += dirty_thresh / 10;      /* wheeee... */

                if (global_page_state(NR_UNSTABLE_NFS) +
			global_page_state(NR_WRITEBACK) <= dirty_thresh)
                        	break;
                congestion_wait(WRITE, HZ/10);

		/*
		 * The caller might hold locks which can prevent IO completion
		 * or progress in the filesystem.  So we cannot just sit here
		 * waiting for IO to complete.
		 */
		if ((gfp_mask & (__GFP_FS|__GFP_IO)) != (__GFP_FS|__GFP_IO))
			break;
        }
}

/*
 * writeback at least _min_pages, and keep writing until the amount of dirty
 * memory is less than the background threshold, or until we're all clean.
 */
static void background_writeout(unsigned long _min_pages)
{
	long min_pages = _min_pages;
	struct writeback_control wbc = {
		.bdi		= NULL,
		.sync_mode	= WB_SYNC_NONE,
		.older_than_this = NULL,
		.nr_to_write	= 0,
		.nonblocking	= 1,
		.range_cyclic	= 1,
	};

	for ( ; ; ) {
		unsigned long background_thresh;
		unsigned long dirty_thresh;

		get_dirty_limits(&background_thresh, &dirty_thresh, NULL, NULL);
		if (global_page_state(NR_FILE_DIRTY) +
			global_page_state(NR_UNSTABLE_NFS) < background_thresh
				&& min_pages <= 0)
			break;
		wbc.more_io = 0;
		wbc.encountered_congestion = 0;
		wbc.nr_to_write = MAX_WRITEBACK_PAGES;
		wbc.pages_skipped = 0;
		writeback_inodes(&wbc);
		min_pages -= MAX_WRITEBACK_PAGES - wbc.nr_to_write;
		if (wbc.nr_to_write > 0 || wbc.pages_skipped > 0) {
			/* Wrote less than expected */
			if (wbc.encountered_congestion || wbc.more_io)
				congestion_wait(WRITE, HZ/10);
			else
				break;
		}
	}
}

/*
 * Start writeback of `nr_pages' pages.  If `nr_pages' is zero, write back
 * the whole world.  Returns 0 if a pdflush thread was dispatched.  Returns
 * -1 if all pdflush threads were busy.
 */
int wakeup_pdflush(long nr_pages)
{
	if (nr_pages == 0)
		nr_pages = global_page_state(NR_FILE_DIRTY) +
				global_page_state(NR_UNSTABLE_NFS);
	return pdflush_operation(background_writeout, nr_pages);
}

static void wb_timer_fn(unsigned long unused);
static void laptop_timer_fn(unsigned long unused);

static DEFINE_TIMER(wb_timer, wb_timer_fn, 0, 0);
static DEFINE_TIMER(laptop_mode_wb_timer, laptop_timer_fn, 0, 0);

/*
 * Periodic writeback of "old" data.
 *
 * Define "old": the first time one of an inode's pages is dirtied, we mark the
 * dirtying-time in the inode's address_space.  So this periodic writeback code
 * just walks the superblock inode list, writing back any inodes which are
 * older than a specific point in time.
 *
 * Try to run once per dirty_writeback_interval.  But if a writeback event
 * takes longer than a dirty_writeback_interval interval, then leave a
 * one-second gap.
 *
 * older_than_this takes precedence over nr_to_write.  So we'll only write back
 * all dirty pages if they are all attached to "old" mappings.
 */
static void wb_kupdate(unsigned long arg)
{
	unsigned long oldest_jif;
	unsigned long start_jif;
	unsigned long next_jif;
	long nr_to_write;
	struct writeback_control wbc = {
		.bdi		= NULL,
		.sync_mode	= WB_SYNC_NONE,
		.older_than_this = &oldest_jif,
		.nr_to_write	= 0,
		.nonblocking	= 1,
		.for_kupdate	= 1,
		.range_cyclic	= 1,
	};

	sync_supers();

	oldest_jif = jiffies - msecs_to_jiffies(dirty_expire_interval * 10);
	start_jif = jiffies;
	next_jif = start_jif + msecs_to_jiffies(dirty_writeback_interval * 10);
	nr_to_write = global_page_state(NR_FILE_DIRTY) +
			global_page_state(NR_UNSTABLE_NFS) +
			(inodes_stat.nr_inodes - inodes_stat.nr_unused);
	while (nr_to_write > 0) {
		wbc.more_io = 0;
		wbc.encountered_congestion = 0;
		wbc.nr_to_write = MAX_WRITEBACK_PAGES;
		writeback_inodes(&wbc);
		if (wbc.nr_to_write > 0) {
			if (wbc.encountered_congestion || wbc.more_io)
				congestion_wait(WRITE, HZ/10);
			else
				break;	/* All the old data is written */
		}
		nr_to_write -= MAX_WRITEBACK_PAGES - wbc.nr_to_write;
	}
	if (time_before(next_jif, jiffies + HZ))
		next_jif = jiffies + HZ;
	if (dirty_writeback_interval)
		mod_timer(&wb_timer, next_jif);
}

/*
 * sysctl handler for /proc/sys/vm/dirty_writeback_centisecs
 */
int dirty_writeback_centisecs_handler(ctl_table *table, int write,
	struct file *file, void __user *buffer, size_t *length, loff_t *ppos)
{
	proc_dointvec(table, write, file, buffer, length, ppos);
	if (dirty_writeback_interval)
		mod_timer(&wb_timer, jiffies +
			msecs_to_jiffies(dirty_writeback_interval * 10));
	else
		del_timer(&wb_timer);
	return 0;
}

static void wb_timer_fn(unsigned long unused)
{
	if (pdflush_operation(wb_kupdate, 0) < 0)
		mod_timer(&wb_timer, jiffies + HZ); /* delay 1 second */
}

static void laptop_flush(unsigned long unused)
{
	sys_sync();
}

static void laptop_timer_fn(unsigned long unused)
{
	pdflush_operation(laptop_flush, 0);
}

/*
 * We've spun up the disk and we're in laptop mode: schedule writeback
 * of all dirty data a few seconds from now.  If the flush is already scheduled
 * then push it back - the user is still using the disk.
 */
void laptop_io_completion(void)
{
	mod_timer(&laptop_mode_wb_timer, jiffies + laptop_mode);
}

/*
 * We're in laptop mode and we've just synced. The sync's writes will have
 * caused another writeback to be scheduled by laptop_io_completion.
 * Nothing needs to be written back anymore, so we unschedule the writeback.
 */
void laptop_sync_completion(void)
{
	del_timer(&laptop_mode_wb_timer);
}

/*
 * If ratelimit_pages is too high then we can get into dirty-data overload
 * if a large number of processes all perform writes at the same time.
 * If it is too low then SMP machines will call the (expensive)
 * get_writeback_state too often.
 *
 * Here we set ratelimit_pages to a level which ensures that when all CPUs are
 * dirtying in parallel, we cannot go more than 3% (1/32) over the dirty memory
 * thresholds before writeback cuts in.
 *
 * But the limit should not be set too high.  Because it also controls the
 * amount of memory which the balance_dirty_pages() caller has to write back.
 * If this is too large then the caller will block on the IO queue all the
 * time.  So limit it to four megabytes - the balance_dirty_pages() caller
 * will write six megabyte chunks, max.
 */

void writeback_set_ratelimit(void)
{
	ratelimit_pages = vm_total_pages / (num_online_cpus() * 32);
	if (ratelimit_pages < 16)
		ratelimit_pages = 16;
	if (ratelimit_pages * PAGE_CACHE_SIZE > 4096 * 1024)
		ratelimit_pages = (4096 * 1024) / PAGE_CACHE_SIZE;
}

static int __cpuinit
ratelimit_handler(struct notifier_block *self, unsigned long u, void *v)
{
	writeback_set_ratelimit();
	return NOTIFY_DONE;
}

static struct notifier_block __cpuinitdata ratelimit_nb = {
	.notifier_call	= ratelimit_handler,
	.next		= NULL,
};

/*
 * Called early on to tune the page writeback dirty limits.
 *
 * We used to scale dirty pages according to how total memory
 * related to pages that could be allocated for buffers (by
 * comparing nr_free_buffer_pages() to vm_total_pages.
 *
 * However, that was when we used "dirty_ratio" to scale with
 * all memory, and we don't do that any more. "dirty_ratio"
 * is now applied to total non-HIGHPAGE memory (by subtracting
 * totalhigh_pages from vm_total_pages), and as such we can't
 * get into the old insane situation any more where we had
 * large amounts of dirty pages compared to a small amount of
 * non-HIGHMEM memory.
 *
 * But we might still want to scale the dirty_ratio by how
 * much memory the box has..
 */
void __init page_writeback_init(void)
{
	int shift;

	mod_timer(&wb_timer,
		  jiffies + msecs_to_jiffies(dirty_writeback_interval * 10));
	writeback_set_ratelimit();
	register_cpu_notifier(&ratelimit_nb);

	shift = calc_period_shift();
	prop_descriptor_init(&vm_completions, shift);
	prop_descriptor_init(&vm_dirties, shift);
}

/**
 * write_cache_pages - walk the list of dirty pages of the given address space and write all of them.
 * @mapping: address space structure to write
 * @wbc: subtract the number of written pages from *@wbc->nr_to_write
 * @writepage: function called for each page
 * @data: data passed to writepage function
 *
 * If a page is already under I/O, write_cache_pages() skips it, even
 * if it's dirty.  This is desirable behaviour for memory-cleaning writeback,
 * but it is INCORRECT for data-integrity system calls such as fsync().  fsync()
 * and msync() need to guarantee that all the data which was dirty at the time
 * the call was made get new I/O started against them.  If wbc->sync_mode is
 * WB_SYNC_ALL then we were called for data integrity and we must wait for
 * existing IO to complete.
 */
int write_cache_pages(struct address_space *mapping,
		      struct writeback_control *wbc, writepage_t writepage,
		      void *data)
{
	struct backing_dev_info *bdi = mapping->backing_dev_info;
	int ret = 0;
	int done = 0;
	struct pagevec pvec;
	int nr_pages;
	pgoff_t uninitialized_var(writeback_index);
	pgoff_t index;
	pgoff_t end;		/* Inclusive */
	pgoff_t done_index;
	int cycled;
	int range_whole = 0;
	long nr_to_write = wbc->nr_to_write;

	if (wbc->nonblocking && bdi_write_congested(bdi)) {
		wbc->encountered_congestion = 1;
		return 0;
	}

	pagevec_init(&pvec, 0);
	if (wbc->range_cyclic) {
		writeback_index = mapping->writeback_index; /* prev offset */
		index = writeback_index;
		if (index == 0)
			cycled = 1;
		else
			cycled = 0;
		end = -1;
	} else {
		index = wbc->range_start >> PAGE_CACHE_SHIFT;
		end = wbc->range_end >> PAGE_CACHE_SHIFT;
		if (wbc->range_start == 0 && wbc->range_end == LLONG_MAX)
			range_whole = 1;
		cycled = 1; /* ignore range_cyclic tests */
	}
retry:
	done_index = index;
	while (!done && (index <= end)) {
		int i;

		nr_pages = pagevec_lookup_tag(&pvec, mapping, &index,
			      PAGECACHE_TAG_DIRTY,
			      min(end - index, (pgoff_t)PAGEVEC_SIZE-1) + 1);
		if (nr_pages == 0)
			break;

		for (i = 0; i < nr_pages; i++) {
			struct page *page = pvec.pages[i];

			/*
			 * At this point, the page may be truncated or
			 * invalidated (changing page->mapping to NULL), or
			 * even swizzled back from swapper_space to tmpfs file
			 * mapping. However, page->index will not change
			 * because we have a reference on the page.
			 */
			if (page->index > end) {
				/*
				 * can't be range_cyclic (1st pass) because
				 * end == -1 in that case.
				 */
				done = 1;
				break;
			}

			done_index = page->index + 1;

			lock_page(page);

			/*
			 * Page truncated or invalidated. We can freely skip it
			 * then, even for data integrity operations: the page
			 * has disappeared concurrently, so there could be no
			 * real expectation of this data interity operation
			 * even if there is now a new, dirty page at the same
			 * pagecache address.
			 */
			if (unlikely(page->mapping != mapping)) {
continue_unlock:
				unlock_page(page);
				continue;
			}

			if (!PageDirty(page)) {
				/* someone wrote it for us */
				goto continue_unlock;
			}

			if (PageWriteback(page)) {
				if (wbc->sync_mode != WB_SYNC_NONE)
					wait_on_page_writeback(page);
				else
					goto continue_unlock;
			}

			BUG_ON(PageWriteback(page));
			if (!clear_page_dirty_for_io(page))
				goto continue_unlock;

			ret = (*writepage)(page, wbc, data);
			if (unlikely(ret)) {
				if (ret == AOP_WRITEPAGE_ACTIVATE) {
					unlock_page(page);
					ret = 0;
				} else {
					/*
					 * done_index is set past this page,
					 * so media errors will not choke
					 * background writeout for the entire
					 * file. This has consequences for
					 * range_cyclic semantics (ie. it may
					 * not be suitable for data integrity
					 * writeout).
					 */
					done = 1;
					break;
				}
 			}

			if (nr_to_write > 0) {
				nr_to_write--;
				if (nr_to_write == 0 &&
				    wbc->sync_mode == WB_SYNC_NONE) {
					/*
					 * We stop writing back only if we are
					 * not doing integrity sync. In case of
					 * integrity sync we have to keep going
					 * because someone may be concurrently
					 * dirtying pages, and we might have
					 * synced a lot of newly appeared dirty
					 * pages, but have not synced all of the
					 * old dirty pages.
					 */
					done = 1;
					break;
				}
			}

			if (wbc->nonblocking && bdi_write_congested(bdi)) {
				wbc->encountered_congestion = 1;
				done = 1;
				break;
			}
		}
		pagevec_release(&pvec);
		cond_resched();
	}
	if (!cycled && !done) {
		/*
		 * range_cyclic:
		 * We hit the last page and there is more work to be done: wrap
		 * back to the start of the file
		 */
		cycled = 1;
		index = 0;
		end = writeback_index - 1;
		goto retry;
	}
	if (!wbc->no_nrwrite_index_update) {
		if (wbc->range_cyclic || (range_whole && nr_to_write > 0))
			mapping->writeback_index = done_index;
		wbc->nr_to_write = nr_to_write;
	}

	return ret;
}
EXPORT_SYMBOL(write_cache_pages);

/*
 * Function used by generic_writepages to call the real writepage
 * function and set the mapping flags on error
 */
static int __writepage(struct page *page, struct writeback_control *wbc,
		       void *data)
{
	struct address_space *mapping = data;
	int ret = mapping->a_ops->writepage(page, wbc);
	mapping_set_error(mapping, ret);
	return ret;
}

/**
 * generic_writepages - walk the list of dirty pages of the given address space and writepage() all of them.
 * @mapping: address space structure to write
 * @wbc: subtract the number of written pages from *@wbc->nr_to_write
 *
 * This is a library function, which implements the writepages()
 * address_space_operation.
 */
int generic_writepages(struct address_space *mapping,
		       struct writeback_control *wbc)
{
	/* deal with chardevs and other special file */
	if (!mapping->a_ops->writepage)
		return 0;

	return write_cache_pages(mapping, wbc, __writepage, mapping);
}

EXPORT_SYMBOL(generic_writepages);

int do_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	int ret;

	if (wbc->nr_to_write <= 0)
		return 0;
	wbc->for_writepages = 1;
	if (mapping->a_ops->writepages)
		ret = mapping->a_ops->writepages(mapping, wbc);
	else
		ret = generic_writepages(mapping, wbc);
	wbc->for_writepages = 0;
	return ret;
}

/**
 * write_one_page - write out a single page and optionally wait on I/O
 * @page: the page to write
 * @wait: if true, wait on writeout
 *
 * The page must be locked by the caller and will be unlocked upon return.
 *
 * write_one_page() returns a negative error code if I/O failed.
 */
int write_one_page(struct page *page, int wait)
{
	struct address_space *mapping = page->mapping;
	int ret = 0;
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_ALL,
		.nr_to_write = 1,
	};

	BUG_ON(!PageLocked(page));

	if (wait)
		wait_on_page_writeback(page);

	if (clear_page_dirty_for_io(page)) {
		page_cache_get(page);
		ret = mapping->a_ops->writepage(page, &wbc);
		if (ret == 0 && wait) {
			wait_on_page_writeback(page);
			if (PageError(page))
				ret = -EIO;
		}
		page_cache_release(page);
	} else {
		unlock_page(page);
	}
	return ret;
}
EXPORT_SYMBOL(write_one_page);

/*
 * For address_spaces which do not use buffers nor write back.
 */
int __set_page_dirty_no_writeback(struct page *page)
{
	if (!PageDirty(page))
		SetPageDirty(page);
	return 0;
}

/*
 * Helper function for set_page_dirty family.
 * NOTE: This relies on being atomic wrt interrupts.
 */
void account_page_dirtied(struct page *page, struct address_space *mapping)
{
	if (mapping_cap_account_dirty(mapping)) {
		__inc_zone_page_state(page, NR_FILE_DIRTY);
		__inc_bdi_stat(mapping->backing_dev_info, BDI_RECLAIMABLE);
		task_dirty_inc(current);
		task_io_account_write(PAGE_CACHE_SIZE);
	}
}

/*
 * For address_spaces which do not use buffers.  Just tag the page as dirty in
 * its radix tree.
 *
 * This is also used when a single buffer is being dirtied: we want to set the
 * page dirty in that case, but not all the buffers.  This is a "bottom-up"
 * dirtying, whereas __set_page_dirty_buffers() is a "top-down" dirtying.
 *
 * Most callers have locked the page, which pins the address_space in memory.
 * But zap_pte_range() does not lock the page, however in that case the
 * mapping is pinned by the vma's ->vm_file reference.
 *
 * We take care to handle the case where the page was truncated from the
 * mapping by re-checking page_mapping() inside tree_lock.
 */
int __set_page_dirty_nobuffers(struct page *page)
{
	if (!TestSetPageDirty(page)) {
		struct address_space *mapping = page_mapping(page);
		struct address_space *mapping2;

		if (!mapping)
			return 1;

		spin_lock_irq(&mapping->tree_lock);
		mapping2 = page_mapping(page);
		if (mapping2) { /* Race with truncate? */
			BUG_ON(mapping2 != mapping);
			WARN_ON_ONCE(!PagePrivate(page) && !PageUptodate(page));
			account_page_dirtied(page, mapping);
			radix_tree_tag_set(&mapping->page_tree,
				page_index(page), PAGECACHE_TAG_DIRTY);
		}
		spin_unlock_irq(&mapping->tree_lock);
		if (mapping->host) {
			/* !PageAnon && !swapper_space */
			__mark_inode_dirty(mapping->host, I_DIRTY_PAGES);
		}
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL(__set_page_dirty_nobuffers);

/*
 * When a writepage implementation decides that it doesn't want to write this
 * page for some reason, it should redirty the locked page via
 * redirty_page_for_writepage() and it should then unlock the page and return 0
 */
int redirty_page_for_writepage(struct writeback_control *wbc, struct page *page)
{
	wbc->pages_skipped++;
	return __set_page_dirty_nobuffers(page);
}
EXPORT_SYMBOL(redirty_page_for_writepage);

/*
 * If the mapping doesn't provide a set_page_dirty a_op, then
 * just fall through and assume that it wants buffer_heads.
 */
int set_page_dirty(struct page *page)
{
	struct address_space *mapping = page_mapping(page);

	if (likely(mapping)) {
		int (*spd)(struct page *) = mapping->a_ops->set_page_dirty;
#ifdef CONFIG_BLOCK
		if (!spd)
			spd = __set_page_dirty_buffers;
#endif
		return (*spd)(page);
	}
	if (!PageDirty(page)) {
		if (!TestSetPageDirty(page))
			return 1;
	}
	return 0;
}
EXPORT_SYMBOL(set_page_dirty);

/*
 * set_page_dirty() is racy if the caller has no reference against
 * page->mapping->host, and if the page is unlocked.  This is because another
 * CPU could truncate the page off the mapping and then free the mapping.
 *
 * Usually, the page _is_ locked, or the caller is a user-space process which
 * holds a reference on the inode by having an open file.
 *
 * In other cases, the page should be locked before running set_page_dirty().
 */
int set_page_dirty_lock(struct page *page)
{
	int ret;

	lock_page_nosync(page);
	ret = set_page_dirty(page);
	unlock_page(page);
	return ret;
}
EXPORT_SYMBOL(set_page_dirty_lock);

/*
 * Clear a page's dirty flag, while caring for dirty memory accounting.
 * Returns true if the page was previously dirty.
 *
 * This is for preparing to put the page under writeout.  We leave the page
 * tagged as dirty in the radix tree so that a concurrent write-for-sync
 * can discover it via a PAGECACHE_TAG_DIRTY walk.  The ->writepage
 * implementation will run either set_page_writeback() or set_page_dirty(),
 * at which stage we bring the page's dirty flag and radix-tree dirty tag
 * back into sync.
 *
 * This incoherency between the page's dirty flag and radix-tree tag is
 * unfortunate, but it only exists while the page is locked.
 */
int clear_page_dirty_for_io(struct page *page)
{
	struct address_space *mapping = page_mapping(page);

	BUG_ON(!PageLocked(page));

	ClearPageReclaim(page);
	if (mapping && mapping_cap_account_dirty(mapping)) {
		/*
		 * Yes, Virginia, this is indeed insane.
		 *
		 * We use this sequence to make sure that
		 *  (a) we account for dirty stats properly
		 *  (b) we tell the low-level filesystem to
		 *      mark the whole page dirty if it was
		 *      dirty in a pagetable. Only to then
		 *  (c) clean the page again and return 1 to
		 *      cause the writeback.
		 *
		 * This way we avoid all nasty races with the
		 * dirty bit in multiple places and clearing
		 * them concurrently from different threads.
		 *
		 * Note! Normally the "set_page_dirty(page)"
		 * has no effect on the actual dirty bit - since
		 * that will already usually be set. But we
		 * need the side effects, and it can help us
		 * avoid races.
		 *
		 * We basically use the page "master dirty bit"
		 * as a serialization point for all the different
		 * threads doing their things.
		 */
		if (page_mkclean(page))
			set_page_dirty(page);
		/*
		 * We carefully synchronise fault handlers against
		 * installing a dirty pte and marking the page dirty
		 * at this point. We do this by having them hold the
		 * page lock at some point after installing their
		 * pte, but before marking the page dirty.
		 * Pages are always locked coming in here, so we get
		 * the desired exclusion. See mm/memory.c:do_wp_page()
		 * for more comments.
		 */
		if (TestClearPageDirty(page)) {
			dec_zone_page_state(page, NR_FILE_DIRTY);
			dec_bdi_stat(mapping->backing_dev_info,
					BDI_RECLAIMABLE);
			return 1;
		}
		return 0;
	}
	return TestClearPageDirty(page);
}
EXPORT_SYMBOL(clear_page_dirty_for_io);

int test_clear_page_writeback(struct page *page)
{
	struct address_space *mapping = page_mapping(page);
	int ret;

	if (mapping) {
		struct backing_dev_info *bdi = mapping->backing_dev_info;
		unsigned long flags;

		spin_lock_irqsave(&mapping->tree_lock, flags);
		ret = TestClearPageWriteback(page);
		if (ret) {
			radix_tree_tag_clear(&mapping->page_tree,
						page_index(page),
						PAGECACHE_TAG_WRITEBACK);
			if (bdi_cap_account_writeback(bdi)) {
				__dec_bdi_stat(bdi, BDI_WRITEBACK);
				__bdi_writeout_inc(bdi);
			}
		}
		spin_unlock_irqrestore(&mapping->tree_lock, flags);
	} else {
		ret = TestClearPageWriteback(page);
	}
	if (ret)
		dec_zone_page_state(page, NR_WRITEBACK);
	return ret;
}

int test_set_page_writeback(struct page *page)
{
	struct address_space *mapping = page_mapping(page);
	int ret;

	if (mapping) {
		struct backing_dev_info *bdi = mapping->backing_dev_info;
		unsigned long flags;

		spin_lock_irqsave(&mapping->tree_lock, flags);
		ret = TestSetPageWriteback(page);
		if (!ret) {
			radix_tree_tag_set(&mapping->page_tree,
						page_index(page),
						PAGECACHE_TAG_WRITEBACK);
			if (bdi_cap_account_writeback(bdi))
				__inc_bdi_stat(bdi, BDI_WRITEBACK);
		}
		if (!PageDirty(page))
			radix_tree_tag_clear(&mapping->page_tree,
						page_index(page),
						PAGECACHE_TAG_DIRTY);
		spin_unlock_irqrestore(&mapping->tree_lock, flags);
	} else {
		ret = TestSetPageWriteback(page);
	}
	if (!ret)
		inc_zone_page_state(page, NR_WRITEBACK);
	return ret;

}
EXPORT_SYMBOL(test_set_page_writeback);

/*
 * Return true if any of the pages in the mapping are marked with the
 * passed tag.
 */
int mapping_tagged(struct address_space *mapping, int tag)
{
	int ret;
	rcu_read_lock();
	ret = radix_tree_tagged(&mapping->page_tree, tag);
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL(mapping_tagged);
