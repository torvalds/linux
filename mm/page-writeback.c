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
#include <linux/export.h>
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
#include <linux/buffer_head.h> /* __set_page_dirty_buffers */
#include <linux/pagevec.h>
#include <linux/timer.h>
#include <linux/sched/rt.h>
#include <trace/events/writeback.h>

/*
 * Sleep at most 200ms at a time in balance_dirty_pages().
 */
#define MAX_PAUSE		max(HZ/5, 1)

/*
 * Try to keep balance_dirty_pages() call intervals higher than this many pages
 * by raising pause time to max_pause when falls below it.
 */
#define DIRTY_POLL_THRESH	(128 >> (PAGE_SHIFT - 10))

/*
 * Estimate write bandwidth at 200ms intervals.
 */
#define BANDWIDTH_INTERVAL	max(HZ/5, 1)

#define RATELIMIT_CALC_SHIFT	10

/*
 * After a CPU has dirtied this many pages, balance_dirty_pages_ratelimited
 * will look to see if it needs to force writeback or throttling.
 */
static long ratelimit_pages = 32;

/* The following parameters are exported via /proc/sys/vm */

/*
 * Start background writeback (via writeback threads) at this percentage
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

EXPORT_SYMBOL_GPL(dirty_writeback_interval);

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

unsigned long global_dirty_limit;

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
static struct fprop_global writeout_completions;

static void writeout_period(unsigned long t);
/* Timer for aging of writeout_completions */
static struct timer_list writeout_period_timer =
		TIMER_DEFERRED_INITIALIZER(writeout_period, 0, 0);
static unsigned long writeout_period_time = 0;

/*
 * Length of period for aging writeout fractions of bdis. This is an
 * arbitrarily chosen number. The longer the period, the slower fractions will
 * reflect changes in current writeout rate.
 */
#define VM_COMPLETIONS_PERIOD_LEN (3*HZ)

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

/*
 * In a memory zone, there is a certain amount of pages we consider
 * available for the page cache, which is essentially the number of
 * free and reclaimable pages, minus some zone reserves to protect
 * lowmem and the ability to uphold the zone's watermarks without
 * requiring writeback.
 *
 * This number of dirtyable pages is the base value of which the
 * user-configurable dirty ratio is the effictive number of pages that
 * are allowed to be actually dirtied.  Per individual zone, or
 * globally by using the sum of dirtyable pages over all zones.
 *
 * Because the user is allowed to specify the dirty limit globally as
 * absolute number of bytes, calculating the per-zone dirty limit can
 * require translating the configured limit into a percentage of
 * global dirtyable memory first.
 */

/**
 * zone_dirtyable_memory - number of dirtyable pages in a zone
 * @zone: the zone
 *
 * Returns the zone's number of pages potentially available for dirty
 * page cache.  This is the base value for the per-zone dirty limits.
 */
static unsigned long zone_dirtyable_memory(struct zone *zone)
{
	unsigned long nr_pages;

	nr_pages = zone_page_state(zone, NR_FREE_PAGES);
	nr_pages -= min(nr_pages, zone->dirty_balance_reserve);

	nr_pages += zone_page_state(zone, NR_INACTIVE_FILE);
	nr_pages += zone_page_state(zone, NR_ACTIVE_FILE);

	return nr_pages;
}

static unsigned long highmem_dirtyable_memory(unsigned long total)
{
#ifdef CONFIG_HIGHMEM
	int node;
	unsigned long x = 0;

	for_each_node_state(node, N_HIGH_MEMORY) {
		struct zone *z = &NODE_DATA(node)->node_zones[ZONE_HIGHMEM];

		x += zone_dirtyable_memory(z);
	}
	/*
	 * Unreclaimable memory (kernel memory or anonymous memory
	 * without swap) can bring down the dirtyable pages below
	 * the zone's dirty balance reserve and the above calculation
	 * will underflow.  However we still want to add in nodes
	 * which are below threshold (negative values) to get a more
	 * accurate calculation but make sure that the total never
	 * underflows.
	 */
	if ((long)x < 0)
		x = 0;

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
 * global_dirtyable_memory - number of globally dirtyable pages
 *
 * Returns the global number of pages potentially available for dirty
 * page cache.  This is the base value for the global dirty limits.
 */
static unsigned long global_dirtyable_memory(void)
{
	unsigned long x;

	x = global_page_state(NR_FREE_PAGES);
	x -= min(x, dirty_balance_reserve);

	x += global_page_state(NR_INACTIVE_FILE);
	x += global_page_state(NR_ACTIVE_FILE);

	if (!vm_highmem_is_dirtyable)
		x -= highmem_dirtyable_memory(x);

	/* Subtract min_free_kbytes */
	x -= min_t(unsigned long, x, min_free_kbytes >> (PAGE_SHIFT - 10));

	return x + 1;	/* Ensure that we never return 0 */
}

/*
 * global_dirty_limits - background-writeback and dirty-throttling thresholds
 *
 * Calculate the dirty thresholds based on sysctl parameters
 * - vm.dirty_background_ratio  or  vm.dirty_background_bytes
 * - vm.dirty_ratio             or  vm.dirty_bytes
 * The dirty limits will be lifted by 1/4 for PF_LESS_THROTTLE (ie. nfsd) and
 * real-time tasks.
 */
void global_dirty_limits(unsigned long *pbackground, unsigned long *pdirty)
{
	unsigned long background;
	unsigned long dirty;
	unsigned long uninitialized_var(available_memory);
	struct task_struct *tsk;

	if (!vm_dirty_bytes || !dirty_background_bytes)
		available_memory = global_dirtyable_memory();

	if (vm_dirty_bytes)
		dirty = DIV_ROUND_UP(vm_dirty_bytes, PAGE_SIZE);
	else
		dirty = (vm_dirty_ratio * available_memory) / 100;

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
	trace_global_dirty_state(background, dirty);
}

/**
 * zone_dirty_limit - maximum number of dirty pages allowed in a zone
 * @zone: the zone
 *
 * Returns the maximum number of dirty pages allowed in a zone, based
 * on the zone's dirtyable memory.
 */
static unsigned long zone_dirty_limit(struct zone *zone)
{
	unsigned long zone_memory = zone_dirtyable_memory(zone);
	struct task_struct *tsk = current;
	unsigned long dirty;

	if (vm_dirty_bytes)
		dirty = DIV_ROUND_UP(vm_dirty_bytes, PAGE_SIZE) *
			zone_memory / global_dirtyable_memory();
	else
		dirty = vm_dirty_ratio * zone_memory / 100;

	if (tsk->flags & PF_LESS_THROTTLE || rt_task(tsk))
		dirty += dirty / 4;

	return dirty;
}

/**
 * zone_dirty_ok - tells whether a zone is within its dirty limits
 * @zone: the zone to check
 *
 * Returns %true when the dirty pages in @zone are within the zone's
 * dirty limit, %false if the limit is exceeded.
 */
bool zone_dirty_ok(struct zone *zone)
{
	unsigned long limit = zone_dirty_limit(zone);

	return zone_page_state(zone, NR_FILE_DIRTY) +
	       zone_page_state(zone, NR_UNSTABLE_NFS) +
	       zone_page_state(zone, NR_WRITEBACK) <= limit;
}

int dirty_background_ratio_handler(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp,
		loff_t *ppos)
{
	int ret;

	ret = proc_dointvec_minmax(table, write, buffer, lenp, ppos);
	if (ret == 0 && write)
		dirty_background_bytes = 0;
	return ret;
}

int dirty_background_bytes_handler(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp,
		loff_t *ppos)
{
	int ret;

	ret = proc_doulongvec_minmax(table, write, buffer, lenp, ppos);
	if (ret == 0 && write)
		dirty_background_ratio = 0;
	return ret;
}

int dirty_ratio_handler(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp,
		loff_t *ppos)
{
	int old_ratio = vm_dirty_ratio;
	int ret;

	ret = proc_dointvec_minmax(table, write, buffer, lenp, ppos);
	if (ret == 0 && write && vm_dirty_ratio != old_ratio) {
		writeback_set_ratelimit();
		vm_dirty_bytes = 0;
	}
	return ret;
}

int dirty_bytes_handler(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp,
		loff_t *ppos)
{
	unsigned long old_bytes = vm_dirty_bytes;
	int ret;

	ret = proc_doulongvec_minmax(table, write, buffer, lenp, ppos);
	if (ret == 0 && write && vm_dirty_bytes != old_bytes) {
		writeback_set_ratelimit();
		vm_dirty_ratio = 0;
	}
	return ret;
}

static unsigned long wp_next_time(unsigned long cur_time)
{
	cur_time += VM_COMPLETIONS_PERIOD_LEN;
	/* 0 has a special meaning... */
	if (!cur_time)
		return 1;
	return cur_time;
}

/*
 * Increment the BDI's writeout completion count and the global writeout
 * completion count. Called from test_clear_page_writeback().
 */
static inline void __bdi_writeout_inc(struct backing_dev_info *bdi)
{
	__inc_bdi_stat(bdi, BDI_WRITTEN);
	__fprop_inc_percpu_max(&writeout_completions, &bdi->completions,
			       bdi->max_prop_frac);
	/* First event after period switching was turned off? */
	if (!unlikely(writeout_period_time)) {
		/*
		 * We can race with other __bdi_writeout_inc calls here but
		 * it does not cause any harm since the resulting time when
		 * timer will fire and what is in writeout_period_time will be
		 * roughly the same.
		 */
		writeout_period_time = wp_next_time(jiffies);
		mod_timer(&writeout_period_timer, writeout_period_time);
	}
}

void bdi_writeout_inc(struct backing_dev_info *bdi)
{
	unsigned long flags;

	local_irq_save(flags);
	__bdi_writeout_inc(bdi);
	local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(bdi_writeout_inc);

/*
 * Obtain an accurate fraction of the BDI's portion.
 */
static void bdi_writeout_fraction(struct backing_dev_info *bdi,
		long *numerator, long *denominator)
{
	fprop_fraction_percpu(&writeout_completions, &bdi->completions,
				numerator, denominator);
}

/*
 * On idle system, we can be called long after we scheduled because we use
 * deferred timers so count with missed periods.
 */
static void writeout_period(unsigned long t)
{
	int miss_periods = (jiffies - writeout_period_time) /
						 VM_COMPLETIONS_PERIOD_LEN;

	if (fprop_new_period(&writeout_completions, miss_periods + 1)) {
		writeout_period_time = wp_next_time(writeout_period_time +
				miss_periods * VM_COMPLETIONS_PERIOD_LEN);
		mod_timer(&writeout_period_timer, writeout_period_time);
	} else {
		/*
		 * Aging has zeroed all fractions. Stop wasting CPU on period
		 * updates.
		 */
		writeout_period_time = 0;
	}
}

/*
 * bdi_min_ratio keeps the sum of the minimum dirty shares of all
 * registered backing devices, which, for obvious reasons, can not
 * exceed 100%.
 */
static unsigned int bdi_min_ratio;

int bdi_set_min_ratio(struct backing_dev_info *bdi, unsigned int min_ratio)
{
	int ret = 0;

	spin_lock_bh(&bdi_lock);
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
	spin_unlock_bh(&bdi_lock);

	return ret;
}

int bdi_set_max_ratio(struct backing_dev_info *bdi, unsigned max_ratio)
{
	int ret = 0;

	if (max_ratio > 100)
		return -EINVAL;

	spin_lock_bh(&bdi_lock);
	if (bdi->min_ratio > max_ratio) {
		ret = -EINVAL;
	} else {
		bdi->max_ratio = max_ratio;
		bdi->max_prop_frac = (FPROP_FRAC_BASE * max_ratio) / 100;
	}
	spin_unlock_bh(&bdi_lock);

	return ret;
}
EXPORT_SYMBOL(bdi_set_max_ratio);

static unsigned long dirty_freerun_ceiling(unsigned long thresh,
					   unsigned long bg_thresh)
{
	return (thresh + bg_thresh) / 2;
}

static unsigned long hard_dirty_limit(unsigned long thresh)
{
	return max(thresh, global_dirty_limit);
}

/**
 * bdi_dirty_limit - @bdi's share of dirty throttling threshold
 * @bdi: the backing_dev_info to query
 * @dirty: global dirty limit in pages
 *
 * Returns @bdi's dirty limit in pages. The term "dirty" in the context of
 * dirty balancing includes all PG_dirty, PG_writeback and NFS unstable pages.
 *
 * Note that balance_dirty_pages() will only seriously take it as a hard limit
 * when sleeping max_pause per page is not enough to keep the dirty pages under
 * control. For example, when the device is completely stalled due to some error
 * conditions, or when there are 1000 dd tasks writing to a slow 10MB/s USB key.
 * In the other normal situations, it acts more gently by throttling the tasks
 * more (rather than completely block them) when the bdi dirty pages go high.
 *
 * It allocates high/low dirty limits to fast/slow devices, in order to prevent
 * - starving fast devices
 * - piling up dirty pages (that will take long time to sync) on slow devices
 *
 * The bdi's share of dirty limit will be adapting to its throughput and
 * bounded by the bdi->min_ratio and/or bdi->max_ratio parameters, if set.
 */
unsigned long bdi_dirty_limit(struct backing_dev_info *bdi, unsigned long dirty)
{
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

	return bdi_dirty;
}

/*
 * Dirty position control.
 *
 * (o) global/bdi setpoints
 *
 * We want the dirty pages be balanced around the global/bdi setpoints.
 * When the number of dirty pages is higher/lower than the setpoint, the
 * dirty position control ratio (and hence task dirty ratelimit) will be
 * decreased/increased to bring the dirty pages back to the setpoint.
 *
 *     pos_ratio = 1 << RATELIMIT_CALC_SHIFT
 *
 *     if (dirty < setpoint) scale up   pos_ratio
 *     if (dirty > setpoint) scale down pos_ratio
 *
 *     if (bdi_dirty < bdi_setpoint) scale up   pos_ratio
 *     if (bdi_dirty > bdi_setpoint) scale down pos_ratio
 *
 *     task_ratelimit = dirty_ratelimit * pos_ratio >> RATELIMIT_CALC_SHIFT
 *
 * (o) global control line
 *
 *     ^ pos_ratio
 *     |
 *     |            |<===== global dirty control scope ======>|
 * 2.0 .............*
 *     |            .*
 *     |            . *
 *     |            .   *
 *     |            .     *
 *     |            .        *
 *     |            .            *
 * 1.0 ................................*
 *     |            .                  .     *
 *     |            .                  .          *
 *     |            .                  .              *
 *     |            .                  .                 *
 *     |            .                  .                    *
 *   0 +------------.------------------.----------------------*------------->
 *           freerun^          setpoint^                 limit^   dirty pages
 *
 * (o) bdi control line
 *
 *     ^ pos_ratio
 *     |
 *     |            *
 *     |              *
 *     |                *
 *     |                  *
 *     |                    * |<=========== span ============>|
 * 1.0 .......................*
 *     |                      . *
 *     |                      .   *
 *     |                      .     *
 *     |                      .       *
 *     |                      .         *
 *     |                      .           *
 *     |                      .             *
 *     |                      .               *
 *     |                      .                 *
 *     |                      .                   *
 *     |                      .                     *
 * 1/4 ...............................................* * * * * * * * * * * *
 *     |                      .                         .
 *     |                      .                           .
 *     |                      .                             .
 *   0 +----------------------.-------------------------------.------------->
 *                bdi_setpoint^                    x_intercept^
 *
 * The bdi control line won't drop below pos_ratio=1/4, so that bdi_dirty can
 * be smoothly throttled down to normal if it starts high in situations like
 * - start writing to a slow SD card and a fast disk at the same time. The SD
 *   card's bdi_dirty may rush to many times higher than bdi_setpoint.
 * - the bdi dirty thresh drops quickly due to change of JBOD workload
 */
static unsigned long bdi_position_ratio(struct backing_dev_info *bdi,
					unsigned long thresh,
					unsigned long bg_thresh,
					unsigned long dirty,
					unsigned long bdi_thresh,
					unsigned long bdi_dirty)
{
	unsigned long write_bw = bdi->avg_write_bandwidth;
	unsigned long freerun = dirty_freerun_ceiling(thresh, bg_thresh);
	unsigned long limit = hard_dirty_limit(thresh);
	unsigned long x_intercept;
	unsigned long setpoint;		/* dirty pages' target balance point */
	unsigned long bdi_setpoint;
	unsigned long span;
	long long pos_ratio;		/* for scaling up/down the rate limit */
	long x;

	if (unlikely(dirty >= limit))
		return 0;

	/*
	 * global setpoint
	 *
	 *                           setpoint - dirty 3
	 *        f(dirty) := 1.0 + (----------------)
	 *                           limit - setpoint
	 *
	 * it's a 3rd order polynomial that subjects to
	 *
	 * (1) f(freerun)  = 2.0 => rampup dirty_ratelimit reasonably fast
	 * (2) f(setpoint) = 1.0 => the balance point
	 * (3) f(limit)    = 0   => the hard limit
	 * (4) df/dx      <= 0	 => negative feedback control
	 * (5) the closer to setpoint, the smaller |df/dx| (and the reverse)
	 *     => fast response on large errors; small oscillation near setpoint
	 */
	setpoint = (freerun + limit) / 2;
	x = div_s64(((s64)setpoint - (s64)dirty) << RATELIMIT_CALC_SHIFT,
		    limit - setpoint + 1);
	pos_ratio = x;
	pos_ratio = pos_ratio * x >> RATELIMIT_CALC_SHIFT;
	pos_ratio = pos_ratio * x >> RATELIMIT_CALC_SHIFT;
	pos_ratio += 1 << RATELIMIT_CALC_SHIFT;

	/*
	 * We have computed basic pos_ratio above based on global situation. If
	 * the bdi is over/under its share of dirty pages, we want to scale
	 * pos_ratio further down/up. That is done by the following mechanism.
	 */

	/*
	 * bdi setpoint
	 *
	 *        f(bdi_dirty) := 1.0 + k * (bdi_dirty - bdi_setpoint)
	 *
	 *                        x_intercept - bdi_dirty
	 *                     := --------------------------
	 *                        x_intercept - bdi_setpoint
	 *
	 * The main bdi control line is a linear function that subjects to
	 *
	 * (1) f(bdi_setpoint) = 1.0
	 * (2) k = - 1 / (8 * write_bw)  (in single bdi case)
	 *     or equally: x_intercept = bdi_setpoint + 8 * write_bw
	 *
	 * For single bdi case, the dirty pages are observed to fluctuate
	 * regularly within range
	 *        [bdi_setpoint - write_bw/2, bdi_setpoint + write_bw/2]
	 * for various filesystems, where (2) can yield in a reasonable 12.5%
	 * fluctuation range for pos_ratio.
	 *
	 * For JBOD case, bdi_thresh (not bdi_dirty!) could fluctuate up to its
	 * own size, so move the slope over accordingly and choose a slope that
	 * yields 100% pos_ratio fluctuation on suddenly doubled bdi_thresh.
	 */
	if (unlikely(bdi_thresh > thresh))
		bdi_thresh = thresh;
	/*
	 * It's very possible that bdi_thresh is close to 0 not because the
	 * device is slow, but that it has remained inactive for long time.
	 * Honour such devices a reasonable good (hopefully IO efficient)
	 * threshold, so that the occasional writes won't be blocked and active
	 * writes can rampup the threshold quickly.
	 */
	bdi_thresh = max(bdi_thresh, (limit - dirty) / 8);
	/*
	 * scale global setpoint to bdi's:
	 *	bdi_setpoint = setpoint * bdi_thresh / thresh
	 */
	x = div_u64((u64)bdi_thresh << 16, thresh + 1);
	bdi_setpoint = setpoint * (u64)x >> 16;
	/*
	 * Use span=(8*write_bw) in single bdi case as indicated by
	 * (thresh - bdi_thresh ~= 0) and transit to bdi_thresh in JBOD case.
	 *
	 *        bdi_thresh                    thresh - bdi_thresh
	 * span = ---------- * (8 * write_bw) + ------------------- * bdi_thresh
	 *          thresh                            thresh
	 */
	span = (thresh - bdi_thresh + 8 * write_bw) * (u64)x >> 16;
	x_intercept = bdi_setpoint + span;

	if (bdi_dirty < x_intercept - span / 4) {
		pos_ratio = div_u64(pos_ratio * (x_intercept - bdi_dirty),
				    x_intercept - bdi_setpoint + 1);
	} else
		pos_ratio /= 4;

	/*
	 * bdi reserve area, safeguard against dirty pool underrun and disk idle
	 * It may push the desired control point of global dirty pages higher
	 * than setpoint.
	 */
	x_intercept = bdi_thresh / 2;
	if (bdi_dirty < x_intercept) {
		if (bdi_dirty > x_intercept / 8)
			pos_ratio = div_u64(pos_ratio * x_intercept, bdi_dirty);
		else
			pos_ratio *= 8;
	}

	return pos_ratio;
}

static void bdi_update_write_bandwidth(struct backing_dev_info *bdi,
				       unsigned long elapsed,
				       unsigned long written)
{
	const unsigned long period = roundup_pow_of_two(3 * HZ);
	unsigned long avg = bdi->avg_write_bandwidth;
	unsigned long old = bdi->write_bandwidth;
	u64 bw;

	/*
	 * bw = written * HZ / elapsed
	 *
	 *                   bw * elapsed + write_bandwidth * (period - elapsed)
	 * write_bandwidth = ---------------------------------------------------
	 *                                          period
	 */
	bw = written - bdi->written_stamp;
	bw *= HZ;
	if (unlikely(elapsed > period)) {
		do_div(bw, elapsed);
		avg = bw;
		goto out;
	}
	bw += (u64)bdi->write_bandwidth * (period - elapsed);
	bw >>= ilog2(period);

	/*
	 * one more level of smoothing, for filtering out sudden spikes
	 */
	if (avg > old && old >= (unsigned long)bw)
		avg -= (avg - old) >> 3;

	if (avg < old && old <= (unsigned long)bw)
		avg += (old - avg) >> 3;

out:
	bdi->write_bandwidth = bw;
	bdi->avg_write_bandwidth = avg;
}

/*
 * The global dirtyable memory and dirty threshold could be suddenly knocked
 * down by a large amount (eg. on the startup of KVM in a swapless system).
 * This may throw the system into deep dirty exceeded state and throttle
 * heavy/light dirtiers alike. To retain good responsiveness, maintain
 * global_dirty_limit for tracking slowly down to the knocked down dirty
 * threshold.
 */
static void update_dirty_limit(unsigned long thresh, unsigned long dirty)
{
	unsigned long limit = global_dirty_limit;

	/*
	 * Follow up in one step.
	 */
	if (limit < thresh) {
		limit = thresh;
		goto update;
	}

	/*
	 * Follow down slowly. Use the higher one as the target, because thresh
	 * may drop below dirty. This is exactly the reason to introduce
	 * global_dirty_limit which is guaranteed to lie above the dirty pages.
	 */
	thresh = max(thresh, dirty);
	if (limit > thresh) {
		limit -= (limit - thresh) >> 5;
		goto update;
	}
	return;
update:
	global_dirty_limit = limit;
}

static void global_update_bandwidth(unsigned long thresh,
				    unsigned long dirty,
				    unsigned long now)
{
	static DEFINE_SPINLOCK(dirty_lock);
	static unsigned long update_time;

	/*
	 * check locklessly first to optimize away locking for the most time
	 */
	if (time_before(now, update_time + BANDWIDTH_INTERVAL))
		return;

	spin_lock(&dirty_lock);
	if (time_after_eq(now, update_time + BANDWIDTH_INTERVAL)) {
		update_dirty_limit(thresh, dirty);
		update_time = now;
	}
	spin_unlock(&dirty_lock);
}

/*
 * Maintain bdi->dirty_ratelimit, the base dirty throttle rate.
 *
 * Normal bdi tasks will be curbed at or below it in long term.
 * Obviously it should be around (write_bw / N) when there are N dd tasks.
 */
static void bdi_update_dirty_ratelimit(struct backing_dev_info *bdi,
				       unsigned long thresh,
				       unsigned long bg_thresh,
				       unsigned long dirty,
				       unsigned long bdi_thresh,
				       unsigned long bdi_dirty,
				       unsigned long dirtied,
				       unsigned long elapsed)
{
	unsigned long freerun = dirty_freerun_ceiling(thresh, bg_thresh);
	unsigned long limit = hard_dirty_limit(thresh);
	unsigned long setpoint = (freerun + limit) / 2;
	unsigned long write_bw = bdi->avg_write_bandwidth;
	unsigned long dirty_ratelimit = bdi->dirty_ratelimit;
	unsigned long dirty_rate;
	unsigned long task_ratelimit;
	unsigned long balanced_dirty_ratelimit;
	unsigned long pos_ratio;
	unsigned long step;
	unsigned long x;

	/*
	 * The dirty rate will match the writeout rate in long term, except
	 * when dirty pages are truncated by userspace or re-dirtied by FS.
	 */
	dirty_rate = (dirtied - bdi->dirtied_stamp) * HZ / elapsed;

	pos_ratio = bdi_position_ratio(bdi, thresh, bg_thresh, dirty,
				       bdi_thresh, bdi_dirty);
	/*
	 * task_ratelimit reflects each dd's dirty rate for the past 200ms.
	 */
	task_ratelimit = (u64)dirty_ratelimit *
					pos_ratio >> RATELIMIT_CALC_SHIFT;
	task_ratelimit++; /* it helps rampup dirty_ratelimit from tiny values */

	/*
	 * A linear estimation of the "balanced" throttle rate. The theory is,
	 * if there are N dd tasks, each throttled at task_ratelimit, the bdi's
	 * dirty_rate will be measured to be (N * task_ratelimit). So the below
	 * formula will yield the balanced rate limit (write_bw / N).
	 *
	 * Note that the expanded form is not a pure rate feedback:
	 *	rate_(i+1) = rate_(i) * (write_bw / dirty_rate)		     (1)
	 * but also takes pos_ratio into account:
	 *	rate_(i+1) = rate_(i) * (write_bw / dirty_rate) * pos_ratio  (2)
	 *
	 * (1) is not realistic because pos_ratio also takes part in balancing
	 * the dirty rate.  Consider the state
	 *	pos_ratio = 0.5						     (3)
	 *	rate = 2 * (write_bw / N)				     (4)
	 * If (1) is used, it will stuck in that state! Because each dd will
	 * be throttled at
	 *	task_ratelimit = pos_ratio * rate = (write_bw / N)	     (5)
	 * yielding
	 *	dirty_rate = N * task_ratelimit = write_bw		     (6)
	 * put (6) into (1) we get
	 *	rate_(i+1) = rate_(i)					     (7)
	 *
	 * So we end up using (2) to always keep
	 *	rate_(i+1) ~= (write_bw / N)				     (8)
	 * regardless of the value of pos_ratio. As long as (8) is satisfied,
	 * pos_ratio is able to drive itself to 1.0, which is not only where
	 * the dirty count meet the setpoint, but also where the slope of
	 * pos_ratio is most flat and hence task_ratelimit is least fluctuated.
	 */
	balanced_dirty_ratelimit = div_u64((u64)task_ratelimit * write_bw,
					   dirty_rate | 1);
	/*
	 * balanced_dirty_ratelimit ~= (write_bw / N) <= write_bw
	 */
	if (unlikely(balanced_dirty_ratelimit > write_bw))
		balanced_dirty_ratelimit = write_bw;

	/*
	 * We could safely do this and return immediately:
	 *
	 *	bdi->dirty_ratelimit = balanced_dirty_ratelimit;
	 *
	 * However to get a more stable dirty_ratelimit, the below elaborated
	 * code makes use of task_ratelimit to filter out singular points and
	 * limit the step size.
	 *
	 * The below code essentially only uses the relative value of
	 *
	 *	task_ratelimit - dirty_ratelimit
	 *	= (pos_ratio - 1) * dirty_ratelimit
	 *
	 * which reflects the direction and size of dirty position error.
	 */

	/*
	 * dirty_ratelimit will follow balanced_dirty_ratelimit iff
	 * task_ratelimit is on the same side of dirty_ratelimit, too.
	 * For example, when
	 * - dirty_ratelimit > balanced_dirty_ratelimit
	 * - dirty_ratelimit > task_ratelimit (dirty pages are above setpoint)
	 * lowering dirty_ratelimit will help meet both the position and rate
	 * control targets. Otherwise, don't update dirty_ratelimit if it will
	 * only help meet the rate target. After all, what the users ultimately
	 * feel and care are stable dirty rate and small position error.
	 *
	 * |task_ratelimit - dirty_ratelimit| is used to limit the step size
	 * and filter out the singular points of balanced_dirty_ratelimit. Which
	 * keeps jumping around randomly and can even leap far away at times
	 * due to the small 200ms estimation period of dirty_rate (we want to
	 * keep that period small to reduce time lags).
	 */
	step = 0;
	if (dirty < setpoint) {
		x = min(bdi->balanced_dirty_ratelimit,
			 min(balanced_dirty_ratelimit, task_ratelimit));
		if (dirty_ratelimit < x)
			step = x - dirty_ratelimit;
	} else {
		x = max(bdi->balanced_dirty_ratelimit,
			 max(balanced_dirty_ratelimit, task_ratelimit));
		if (dirty_ratelimit > x)
			step = dirty_ratelimit - x;
	}

	/*
	 * Don't pursue 100% rate matching. It's impossible since the balanced
	 * rate itself is constantly fluctuating. So decrease the track speed
	 * when it gets close to the target. Helps eliminate pointless tremors.
	 */
	step >>= dirty_ratelimit / (2 * step + 1);
	/*
	 * Limit the tracking speed to avoid overshooting.
	 */
	step = (step + 7) / 8;

	if (dirty_ratelimit < balanced_dirty_ratelimit)
		dirty_ratelimit += step;
	else
		dirty_ratelimit -= step;

	bdi->dirty_ratelimit = max(dirty_ratelimit, 1UL);
	bdi->balanced_dirty_ratelimit = balanced_dirty_ratelimit;

	trace_bdi_dirty_ratelimit(bdi, dirty_rate, task_ratelimit);
}

void __bdi_update_bandwidth(struct backing_dev_info *bdi,
			    unsigned long thresh,
			    unsigned long bg_thresh,
			    unsigned long dirty,
			    unsigned long bdi_thresh,
			    unsigned long bdi_dirty,
			    unsigned long start_time)
{
	unsigned long now = jiffies;
	unsigned long elapsed = now - bdi->bw_time_stamp;
	unsigned long dirtied;
	unsigned long written;

	/*
	 * rate-limit, only update once every 200ms.
	 */
	if (elapsed < BANDWIDTH_INTERVAL)
		return;

	dirtied = percpu_counter_read(&bdi->bdi_stat[BDI_DIRTIED]);
	written = percpu_counter_read(&bdi->bdi_stat[BDI_WRITTEN]);

	/*
	 * Skip quiet periods when disk bandwidth is under-utilized.
	 * (at least 1s idle time between two flusher runs)
	 */
	if (elapsed > HZ && time_before(bdi->bw_time_stamp, start_time))
		goto snapshot;

	if (thresh) {
		global_update_bandwidth(thresh, dirty, now);
		bdi_update_dirty_ratelimit(bdi, thresh, bg_thresh, dirty,
					   bdi_thresh, bdi_dirty,
					   dirtied, elapsed);
	}
	bdi_update_write_bandwidth(bdi, elapsed, written);

snapshot:
	bdi->dirtied_stamp = dirtied;
	bdi->written_stamp = written;
	bdi->bw_time_stamp = now;
}

static void bdi_update_bandwidth(struct backing_dev_info *bdi,
				 unsigned long thresh,
				 unsigned long bg_thresh,
				 unsigned long dirty,
				 unsigned long bdi_thresh,
				 unsigned long bdi_dirty,
				 unsigned long start_time)
{
	if (time_is_after_eq_jiffies(bdi->bw_time_stamp + BANDWIDTH_INTERVAL))
		return;
	spin_lock(&bdi->wb.list_lock);
	__bdi_update_bandwidth(bdi, thresh, bg_thresh, dirty,
			       bdi_thresh, bdi_dirty, start_time);
	spin_unlock(&bdi->wb.list_lock);
}

/*
 * After a task dirtied this many pages, balance_dirty_pages_ratelimited()
 * will look to see if it needs to start dirty throttling.
 *
 * If dirty_poll_interval is too low, big NUMA machines will call the expensive
 * global_page_state() too often. So scale it near-sqrt to the safety margin
 * (the number of pages we may dirty without exceeding the dirty limits).
 */
static unsigned long dirty_poll_interval(unsigned long dirty,
					 unsigned long thresh)
{
	if (thresh > dirty)
		return 1UL << (ilog2(thresh - dirty) >> 1);

	return 1;
}

static unsigned long bdi_max_pause(struct backing_dev_info *bdi,
				   unsigned long bdi_dirty)
{
	unsigned long bw = bdi->avg_write_bandwidth;
	unsigned long t;

	/*
	 * Limit pause time for small memory systems. If sleeping for too long
	 * time, a small pool of dirty/writeback pages may go empty and disk go
	 * idle.
	 *
	 * 8 serves as the safety ratio.
	 */
	t = bdi_dirty / (1 + bw / roundup_pow_of_two(1 + HZ / 8));
	t++;

	return min_t(unsigned long, t, MAX_PAUSE);
}

static long bdi_min_pause(struct backing_dev_info *bdi,
			  long max_pause,
			  unsigned long task_ratelimit,
			  unsigned long dirty_ratelimit,
			  int *nr_dirtied_pause)
{
	long hi = ilog2(bdi->avg_write_bandwidth);
	long lo = ilog2(bdi->dirty_ratelimit);
	long t;		/* target pause */
	long pause;	/* estimated next pause */
	int pages;	/* target nr_dirtied_pause */

	/* target for 10ms pause on 1-dd case */
	t = max(1, HZ / 100);

	/*
	 * Scale up pause time for concurrent dirtiers in order to reduce CPU
	 * overheads.
	 *
	 * (N * 10ms) on 2^N concurrent tasks.
	 */
	if (hi > lo)
		t += (hi - lo) * (10 * HZ) / 1024;

	/*
	 * This is a bit convoluted. We try to base the next nr_dirtied_pause
	 * on the much more stable dirty_ratelimit. However the next pause time
	 * will be computed based on task_ratelimit and the two rate limits may
	 * depart considerably at some time. Especially if task_ratelimit goes
	 * below dirty_ratelimit/2 and the target pause is max_pause, the next
	 * pause time will be max_pause*2 _trimmed down_ to max_pause.  As a
	 * result task_ratelimit won't be executed faithfully, which could
	 * eventually bring down dirty_ratelimit.
	 *
	 * We apply two rules to fix it up:
	 * 1) try to estimate the next pause time and if necessary, use a lower
	 *    nr_dirtied_pause so as not to exceed max_pause. When this happens,
	 *    nr_dirtied_pause will be "dancing" with task_ratelimit.
	 * 2) limit the target pause time to max_pause/2, so that the normal
	 *    small fluctuations of task_ratelimit won't trigger rule (1) and
	 *    nr_dirtied_pause will remain as stable as dirty_ratelimit.
	 */
	t = min(t, 1 + max_pause / 2);
	pages = dirty_ratelimit * t / roundup_pow_of_two(HZ);

	/*
	 * Tiny nr_dirtied_pause is found to hurt I/O performance in the test
	 * case fio-mmap-randwrite-64k, which does 16*{sync read, async write}.
	 * When the 16 consecutive reads are often interrupted by some dirty
	 * throttling pause during the async writes, cfq will go into idles
	 * (deadline is fine). So push nr_dirtied_pause as high as possible
	 * until reaches DIRTY_POLL_THRESH=32 pages.
	 */
	if (pages < DIRTY_POLL_THRESH) {
		t = max_pause;
		pages = dirty_ratelimit * t / roundup_pow_of_two(HZ);
		if (pages > DIRTY_POLL_THRESH) {
			pages = DIRTY_POLL_THRESH;
			t = HZ * DIRTY_POLL_THRESH / dirty_ratelimit;
		}
	}

	pause = HZ * pages / (task_ratelimit + 1);
	if (pause > max_pause) {
		t = max_pause;
		pages = task_ratelimit * t / roundup_pow_of_two(HZ);
	}

	*nr_dirtied_pause = pages;
	/*
	 * The minimal pause time will normally be half the target pause time.
	 */
	return pages >= DIRTY_POLL_THRESH ? 1 + t / 2 : t;
}

/*
 * balance_dirty_pages() must be called by processes which are generating dirty
 * data.  It looks at the number of dirty pages in the machine and will force
 * the caller to wait once crossing the (background_thresh + dirty_thresh) / 2.
 * If we're over `background_thresh' then the writeback threads are woken to
 * perform some writeout.
 */
static void balance_dirty_pages(struct address_space *mapping,
				unsigned long pages_dirtied)
{
	unsigned long nr_reclaimable;	/* = file_dirty + unstable_nfs */
	unsigned long bdi_reclaimable;
	unsigned long nr_dirty;  /* = file_dirty + writeback + unstable_nfs */
	unsigned long bdi_dirty;
	unsigned long freerun;
	unsigned long background_thresh;
	unsigned long dirty_thresh;
	unsigned long bdi_thresh;
	long period;
	long pause;
	long max_pause;
	long min_pause;
	int nr_dirtied_pause;
	bool dirty_exceeded = false;
	unsigned long task_ratelimit;
	unsigned long dirty_ratelimit;
	unsigned long pos_ratio;
	struct backing_dev_info *bdi = mapping->backing_dev_info;
	unsigned long start_time = jiffies;

	for (;;) {
		unsigned long now = jiffies;

		/*
		 * Unstable writes are a feature of certain networked
		 * filesystems (i.e. NFS) in which data may have been
		 * written to the server's write cache, but has not yet
		 * been flushed to permanent storage.
		 */
		nr_reclaimable = global_page_state(NR_FILE_DIRTY) +
					global_page_state(NR_UNSTABLE_NFS);
		nr_dirty = nr_reclaimable + global_page_state(NR_WRITEBACK);

		global_dirty_limits(&background_thresh, &dirty_thresh);

		/*
		 * Throttle it only when the background writeback cannot
		 * catch-up. This avoids (excessively) small writeouts
		 * when the bdi limits are ramping up.
		 */
		freerun = dirty_freerun_ceiling(dirty_thresh,
						background_thresh);
		if (nr_dirty <= freerun) {
			current->dirty_paused_when = now;
			current->nr_dirtied = 0;
			current->nr_dirtied_pause =
				dirty_poll_interval(nr_dirty, dirty_thresh);
			break;
		}

		if (unlikely(!writeback_in_progress(bdi)))
			bdi_start_background_writeback(bdi);

		/*
		 * bdi_thresh is not treated as some limiting factor as
		 * dirty_thresh, due to reasons
		 * - in JBOD setup, bdi_thresh can fluctuate a lot
		 * - in a system with HDD and USB key, the USB key may somehow
		 *   go into state (bdi_dirty >> bdi_thresh) either because
		 *   bdi_dirty starts high, or because bdi_thresh drops low.
		 *   In this case we don't want to hard throttle the USB key
		 *   dirtiers for 100 seconds until bdi_dirty drops under
		 *   bdi_thresh. Instead the auxiliary bdi control line in
		 *   bdi_position_ratio() will let the dirtier task progress
		 *   at some rate <= (write_bw / 2) for bringing down bdi_dirty.
		 */
		bdi_thresh = bdi_dirty_limit(bdi, dirty_thresh);

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
		if (bdi_thresh < 2 * bdi_stat_error(bdi)) {
			bdi_reclaimable = bdi_stat_sum(bdi, BDI_RECLAIMABLE);
			bdi_dirty = bdi_reclaimable +
				    bdi_stat_sum(bdi, BDI_WRITEBACK);
		} else {
			bdi_reclaimable = bdi_stat(bdi, BDI_RECLAIMABLE);
			bdi_dirty = bdi_reclaimable +
				    bdi_stat(bdi, BDI_WRITEBACK);
		}

		dirty_exceeded = (bdi_dirty > bdi_thresh) &&
				  (nr_dirty > dirty_thresh);
		if (dirty_exceeded && !bdi->dirty_exceeded)
			bdi->dirty_exceeded = 1;

		bdi_update_bandwidth(bdi, dirty_thresh, background_thresh,
				     nr_dirty, bdi_thresh, bdi_dirty,
				     start_time);

		dirty_ratelimit = bdi->dirty_ratelimit;
		pos_ratio = bdi_position_ratio(bdi, dirty_thresh,
					       background_thresh, nr_dirty,
					       bdi_thresh, bdi_dirty);
		task_ratelimit = ((u64)dirty_ratelimit * pos_ratio) >>
							RATELIMIT_CALC_SHIFT;
		max_pause = bdi_max_pause(bdi, bdi_dirty);
		min_pause = bdi_min_pause(bdi, max_pause,
					  task_ratelimit, dirty_ratelimit,
					  &nr_dirtied_pause);

		if (unlikely(task_ratelimit == 0)) {
			period = max_pause;
			pause = max_pause;
			goto pause;
		}
		period = HZ * pages_dirtied / task_ratelimit;
		pause = period;
		if (current->dirty_paused_when)
			pause -= now - current->dirty_paused_when;
		/*
		 * For less than 1s think time (ext3/4 may block the dirtier
		 * for up to 800ms from time to time on 1-HDD; so does xfs,
		 * however at much less frequency), try to compensate it in
		 * future periods by updating the virtual time; otherwise just
		 * do a reset, as it may be a light dirtier.
		 */
		if (pause < min_pause) {
			trace_balance_dirty_pages(bdi,
						  dirty_thresh,
						  background_thresh,
						  nr_dirty,
						  bdi_thresh,
						  bdi_dirty,
						  dirty_ratelimit,
						  task_ratelimit,
						  pages_dirtied,
						  period,
						  min(pause, 0L),
						  start_time);
			if (pause < -HZ) {
				current->dirty_paused_when = now;
				current->nr_dirtied = 0;
			} else if (period) {
				current->dirty_paused_when += period;
				current->nr_dirtied = 0;
			} else if (current->nr_dirtied_pause <= pages_dirtied)
				current->nr_dirtied_pause += pages_dirtied;
			break;
		}
		if (unlikely(pause > max_pause)) {
			/* for occasional dropped task_ratelimit */
			now += min(pause - max_pause, max_pause);
			pause = max_pause;
		}

pause:
		trace_balance_dirty_pages(bdi,
					  dirty_thresh,
					  background_thresh,
					  nr_dirty,
					  bdi_thresh,
					  bdi_dirty,
					  dirty_ratelimit,
					  task_ratelimit,
					  pages_dirtied,
					  period,
					  pause,
					  start_time);
		__set_current_state(TASK_KILLABLE);
		io_schedule_timeout(pause);

		current->dirty_paused_when = now + pause;
		current->nr_dirtied = 0;
		current->nr_dirtied_pause = nr_dirtied_pause;

		/*
		 * This is typically equal to (nr_dirty < dirty_thresh) and can
		 * also keep "1000+ dd on a slow USB stick" under control.
		 */
		if (task_ratelimit)
			break;

		/*
		 * In the case of an unresponding NFS server and the NFS dirty
		 * pages exceeds dirty_thresh, give the other good bdi's a pipe
		 * to go through, so that tasks on them still remain responsive.
		 *
		 * In theory 1 page is enough to keep the comsumer-producer
		 * pipe going: the flusher cleans 1 page => the task dirties 1
		 * more page. However bdi_dirty has accounting errors.  So use
		 * the larger and more IO friendly bdi_stat_error.
		 */
		if (bdi_dirty <= bdi_stat_error(bdi))
			break;

		if (fatal_signal_pending(current))
			break;
	}

	if (!dirty_exceeded && bdi->dirty_exceeded)
		bdi->dirty_exceeded = 0;

	if (writeback_in_progress(bdi))
		return;

	/*
	 * In laptop mode, we wait until hitting the higher threshold before
	 * starting background writeout, and then write out all the way down
	 * to the lower threshold.  So slow writers cause minimal disk activity.
	 *
	 * In normal mode, we start background writeout at the lower
	 * background_thresh, to keep the amount of dirty memory low.
	 */
	if (laptop_mode)
		return;

	if (nr_reclaimable > background_thresh)
		bdi_start_background_writeback(bdi);
}

void set_page_dirty_balance(struct page *page, int page_mkwrite)
{
	if (set_page_dirty(page) || page_mkwrite) {
		struct address_space *mapping = page_mapping(page);

		if (mapping)
			balance_dirty_pages_ratelimited(mapping);
	}
}

static DEFINE_PER_CPU(int, bdp_ratelimits);

/*
 * Normal tasks are throttled by
 *	loop {
 *		dirty tsk->nr_dirtied_pause pages;
 *		take a snap in balance_dirty_pages();
 *	}
 * However there is a worst case. If every task exit immediately when dirtied
 * (tsk->nr_dirtied_pause - 1) pages, balance_dirty_pages() will never be
 * called to throttle the page dirties. The solution is to save the not yet
 * throttled page dirties in dirty_throttle_leaks on task exit and charge them
 * randomly into the running tasks. This works well for the above worst case,
 * as the new task will pick up and accumulate the old task's leaked dirty
 * count and eventually get throttled.
 */
DEFINE_PER_CPU(int, dirty_throttle_leaks) = 0;

/**
 * balance_dirty_pages_ratelimited - balance dirty memory state
 * @mapping: address_space which was dirtied
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
void balance_dirty_pages_ratelimited(struct address_space *mapping)
{
	struct backing_dev_info *bdi = mapping->backing_dev_info;
	int ratelimit;
	int *p;

	if (!bdi_cap_account_dirty(bdi))
		return;

	ratelimit = current->nr_dirtied_pause;
	if (bdi->dirty_exceeded)
		ratelimit = min(ratelimit, 32 >> (PAGE_SHIFT - 10));

	preempt_disable();
	/*
	 * This prevents one CPU to accumulate too many dirtied pages without
	 * calling into balance_dirty_pages(), which can happen when there are
	 * 1000+ tasks, all of them start dirtying pages at exactly the same
	 * time, hence all honoured too large initial task->nr_dirtied_pause.
	 */
	p =  &__get_cpu_var(bdp_ratelimits);
	if (unlikely(current->nr_dirtied >= ratelimit))
		*p = 0;
	else if (unlikely(*p >= ratelimit_pages)) {
		*p = 0;
		ratelimit = 0;
	}
	/*
	 * Pick up the dirtied pages by the exited tasks. This avoids lots of
	 * short-lived tasks (eg. gcc invocations in a kernel build) escaping
	 * the dirty throttling and livelock other long-run dirtiers.
	 */
	p = &__get_cpu_var(dirty_throttle_leaks);
	if (*p > 0 && current->nr_dirtied < ratelimit) {
		unsigned long nr_pages_dirtied;
		nr_pages_dirtied = min(*p, ratelimit - current->nr_dirtied);
		*p -= nr_pages_dirtied;
		current->nr_dirtied += nr_pages_dirtied;
	}
	preempt_enable();

	if (unlikely(current->nr_dirtied >= ratelimit))
		balance_dirty_pages(mapping, current->nr_dirtied);
}
EXPORT_SYMBOL(balance_dirty_pages_ratelimited);

void throttle_vm_writeout(gfp_t gfp_mask)
{
	unsigned long background_thresh;
	unsigned long dirty_thresh;

        for ( ; ; ) {
		global_dirty_limits(&background_thresh, &dirty_thresh);
		dirty_thresh = hard_dirty_limit(dirty_thresh);

                /*
                 * Boost the allowable dirty threshold a bit for page
                 * allocators so they don't get DoS'ed by heavy writers
                 */
                dirty_thresh += dirty_thresh / 10;      /* wheeee... */

                if (global_page_state(NR_UNSTABLE_NFS) +
			global_page_state(NR_WRITEBACK) <= dirty_thresh)
                        	break;
                congestion_wait(BLK_RW_ASYNC, HZ/10);

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
 * sysctl handler for /proc/sys/vm/dirty_writeback_centisecs
 */
int dirty_writeback_centisecs_handler(ctl_table *table, int write,
	void __user *buffer, size_t *length, loff_t *ppos)
{
	proc_dointvec(table, write, buffer, length, ppos);
	return 0;
}

#ifdef CONFIG_BLOCK
void laptop_mode_timer_fn(unsigned long data)
{
	struct request_queue *q = (struct request_queue *)data;
	int nr_pages = global_page_state(NR_FILE_DIRTY) +
		global_page_state(NR_UNSTABLE_NFS);

	/*
	 * We want to write everything out, not just down to the dirty
	 * threshold
	 */
	if (bdi_has_dirty_io(&q->backing_dev_info))
		bdi_start_writeback(&q->backing_dev_info, nr_pages,
					WB_REASON_LAPTOP_TIMER);
}

/*
 * We've spun up the disk and we're in laptop mode: schedule writeback
 * of all dirty data a few seconds from now.  If the flush is already scheduled
 * then push it back - the user is still using the disk.
 */
void laptop_io_completion(struct backing_dev_info *info)
{
	mod_timer(&info->laptop_mode_wb_timer, jiffies + laptop_mode);
}

/*
 * We're in laptop mode and we've just synced. The sync's writes will have
 * caused another writeback to be scheduled by laptop_io_completion.
 * Nothing needs to be written back anymore, so we unschedule the writeback.
 */
void laptop_sync_completion(void)
{
	struct backing_dev_info *bdi;

	rcu_read_lock();

	list_for_each_entry_rcu(bdi, &bdi_list, bdi_list)
		del_timer(&bdi->laptop_mode_wb_timer);

	rcu_read_unlock();
}
#endif

/*
 * If ratelimit_pages is too high then we can get into dirty-data overload
 * if a large number of processes all perform writes at the same time.
 * If it is too low then SMP machines will call the (expensive)
 * get_writeback_state too often.
 *
 * Here we set ratelimit_pages to a level which ensures that when all CPUs are
 * dirtying in parallel, we cannot go more than 3% (1/32) over the dirty memory
 * thresholds.
 */

void writeback_set_ratelimit(void)
{
	unsigned long background_thresh;
	unsigned long dirty_thresh;
	global_dirty_limits(&background_thresh, &dirty_thresh);
	global_dirty_limit = dirty_thresh;
	ratelimit_pages = dirty_thresh / (num_online_cpus() * 32);
	if (ratelimit_pages < 16)
		ratelimit_pages = 16;
}

static int __cpuinit
ratelimit_handler(struct notifier_block *self, unsigned long action,
		  void *hcpu)
{

	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_ONLINE:
	case CPU_DEAD:
		writeback_set_ratelimit();
		return NOTIFY_OK;
	default:
		return NOTIFY_DONE;
	}
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
	writeback_set_ratelimit();
	register_cpu_notifier(&ratelimit_nb);

	fprop_global_init(&writeout_completions);
}

/**
 * tag_pages_for_writeback - tag pages to be written by write_cache_pages
 * @mapping: address space structure to write
 * @start: starting page index
 * @end: ending page index (inclusive)
 *
 * This function scans the page range from @start to @end (inclusive) and tags
 * all pages that have DIRTY tag set with a special TOWRITE tag. The idea is
 * that write_cache_pages (or whoever calls this function) will then use
 * TOWRITE tag to identify pages eligible for writeback.  This mechanism is
 * used to avoid livelocking of writeback by a process steadily creating new
 * dirty pages in the file (thus it is important for this function to be quick
 * so that it can tag pages faster than a dirtying process can create them).
 */
/*
 * We tag pages in batches of WRITEBACK_TAG_BATCH to reduce tree_lock latency.
 */
void tag_pages_for_writeback(struct address_space *mapping,
			     pgoff_t start, pgoff_t end)
{
#define WRITEBACK_TAG_BATCH 4096
	unsigned long tagged;

	do {
		spin_lock_irq(&mapping->tree_lock);
		tagged = radix_tree_range_tag_if_tagged(&mapping->page_tree,
				&start, end, WRITEBACK_TAG_BATCH,
				PAGECACHE_TAG_DIRTY, PAGECACHE_TAG_TOWRITE);
		spin_unlock_irq(&mapping->tree_lock);
		WARN_ON_ONCE(tagged > WRITEBACK_TAG_BATCH);
		cond_resched();
		/* We check 'start' to handle wrapping when end == ~0UL */
	} while (tagged >= WRITEBACK_TAG_BATCH && start);
}
EXPORT_SYMBOL(tag_pages_for_writeback);

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
 *
 * To avoid livelocks (when other process dirties new pages), we first tag
 * pages which should be written back with TOWRITE tag and only then start
 * writing them. For data-integrity sync we have to be careful so that we do
 * not miss some pages (e.g., because some other process has cleared TOWRITE
 * tag we set). The rule we follow is that TOWRITE tag can be cleared only
 * by the process clearing the DIRTY tag (and submitting the page for IO).
 */
int write_cache_pages(struct address_space *mapping,
		      struct writeback_control *wbc, writepage_t writepage,
		      void *data)
{
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
	int tag;

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
	if (wbc->sync_mode == WB_SYNC_ALL || wbc->tagged_writepages)
		tag = PAGECACHE_TAG_TOWRITE;
	else
		tag = PAGECACHE_TAG_DIRTY;
retry:
	if (wbc->sync_mode == WB_SYNC_ALL || wbc->tagged_writepages)
		tag_pages_for_writeback(mapping, index, end);
	done_index = index;
	while (!done && (index <= end)) {
		int i;

		nr_pages = pagevec_lookup_tag(&pvec, mapping, &index, tag,
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

			done_index = page->index;

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

			trace_wbc_writepage(wbc, mapping->backing_dev_info);
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
					done_index = page->index + 1;
					done = 1;
					break;
				}
			}

			/*
			 * We stop writing back only if we are not doing
			 * integrity sync. In case of integrity sync we have to
			 * keep going until we have written all the pages
			 * we tagged for writeback prior to entering this loop.
			 */
			if (--wbc->nr_to_write <= 0 &&
			    wbc->sync_mode == WB_SYNC_NONE) {
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
	if (wbc->range_cyclic || (range_whole && wbc->nr_to_write > 0))
		mapping->writeback_index = done_index;

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
	struct blk_plug plug;
	int ret;

	/* deal with chardevs and other special file */
	if (!mapping->a_ops->writepage)
		return 0;

	blk_start_plug(&plug);
	ret = write_cache_pages(mapping, wbc, __writepage, mapping);
	blk_finish_plug(&plug);
	return ret;
}

EXPORT_SYMBOL(generic_writepages);

int do_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	int ret;

	if (wbc->nr_to_write <= 0)
		return 0;
	if (mapping->a_ops->writepages)
		ret = mapping->a_ops->writepages(mapping, wbc);
	else
		ret = generic_writepages(mapping, wbc);
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
		return !TestSetPageDirty(page);
	return 0;
}

/*
 * Helper function for set_page_dirty family.
 * NOTE: This relies on being atomic wrt interrupts.
 */
void account_page_dirtied(struct page *page, struct address_space *mapping)
{
	trace_writeback_dirty_page(page, mapping);

	if (mapping_cap_account_dirty(mapping)) {
		__inc_zone_page_state(page, NR_FILE_DIRTY);
		__inc_zone_page_state(page, NR_DIRTIED);
		__inc_bdi_stat(mapping->backing_dev_info, BDI_RECLAIMABLE);
		__inc_bdi_stat(mapping->backing_dev_info, BDI_DIRTIED);
		task_io_account_write(PAGE_CACHE_SIZE);
		current->nr_dirtied++;
		this_cpu_inc(bdp_ratelimits);
	}
}
EXPORT_SYMBOL(account_page_dirtied);

/*
 * Helper function for set_page_writeback family.
 * NOTE: Unlike account_page_dirtied this does not rely on being atomic
 * wrt interrupts.
 */
void account_page_writeback(struct page *page)
{
	inc_zone_page_state(page, NR_WRITEBACK);
}
EXPORT_SYMBOL(account_page_writeback);

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
		unsigned long flags;

		if (!mapping)
			return 1;

		spin_lock_irqsave(&mapping->tree_lock, flags);
		mapping2 = page_mapping(page);
		if (mapping2) { /* Race with truncate? */
			BUG_ON(mapping2 != mapping);
			WARN_ON_ONCE(!PagePrivate(page) && !PageUptodate(page));
			account_page_dirtied(page, mapping);
			radix_tree_tag_set(&mapping->page_tree,
				page_index(page), PAGECACHE_TAG_DIRTY);
		}
		spin_unlock_irqrestore(&mapping->tree_lock, flags);
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
 * Call this whenever redirtying a page, to de-account the dirty counters
 * (NR_DIRTIED, BDI_DIRTIED, tsk->nr_dirtied), so that they match the written
 * counters (NR_WRITTEN, BDI_WRITTEN) in long term. The mismatches will lead to
 * systematic errors in balanced_dirty_ratelimit and the dirty pages position
 * control.
 */
void account_page_redirty(struct page *page)
{
	struct address_space *mapping = page->mapping;
	if (mapping && mapping_cap_account_dirty(mapping)) {
		current->nr_dirtied--;
		dec_zone_page_state(page, NR_DIRTIED);
		dec_bdi_stat(mapping->backing_dev_info, BDI_DIRTIED);
	}
}
EXPORT_SYMBOL(account_page_redirty);

/*
 * When a writepage implementation decides that it doesn't want to write this
 * page for some reason, it should redirty the locked page via
 * redirty_page_for_writepage() and it should then unlock the page and return 0
 */
int redirty_page_for_writepage(struct writeback_control *wbc, struct page *page)
{
	wbc->pages_skipped++;
	account_page_redirty(page);
	return __set_page_dirty_nobuffers(page);
}
EXPORT_SYMBOL(redirty_page_for_writepage);

/*
 * Dirty a page.
 *
 * For pages with a mapping this should be done under the page lock
 * for the benefit of asynchronous memory errors who prefer a consistent
 * dirty state. This rule can be broken in some special cases,
 * but should be better not to.
 *
 * If the mapping doesn't provide a set_page_dirty a_op, then
 * just fall through and assume that it wants buffer_heads.
 */
int set_page_dirty(struct page *page)
{
	struct address_space *mapping = page_mapping(page);

	if (likely(mapping)) {
		int (*spd)(struct page *) = mapping->a_ops->set_page_dirty;
		/*
		 * readahead/lru_deactivate_page could remain
		 * PG_readahead/PG_reclaim due to race with end_page_writeback
		 * About readahead, if the page is written, the flags would be
		 * reset. So no problem.
		 * About lru_deactivate_page, if the page is redirty, the flag
		 * will be reset. So no problem. but if the page is used by readahead
		 * it will confuse readahead and make it restart the size rampup
		 * process. But it's a trivial problem.
		 */
		ClearPageReclaim(page);
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

	lock_page(page);
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
	if (ret) {
		dec_zone_page_state(page, NR_WRITEBACK);
		inc_zone_page_state(page, NR_WRITTEN);
	}
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
		radix_tree_tag_clear(&mapping->page_tree,
				     page_index(page),
				     PAGECACHE_TAG_TOWRITE);
		spin_unlock_irqrestore(&mapping->tree_lock, flags);
	} else {
		ret = TestSetPageWriteback(page);
	}
	if (!ret)
		account_page_writeback(page);
	return ret;

}
EXPORT_SYMBOL(test_set_page_writeback);

/*
 * Return true if any of the pages in the mapping are marked with the
 * passed tag.
 */
int mapping_tagged(struct address_space *mapping, int tag)
{
	return radix_tree_tagged(&mapping->page_tree, tag);
}
EXPORT_SYMBOL(mapping_tagged);

/**
 * wait_for_stable_page() - wait for writeback to finish, if necessary.
 * @page:	The page to wait on.
 *
 * This function determines if the given page is related to a backing device
 * that requires page contents to be held stable during writeback.  If so, then
 * it will wait for any pending writeback to complete.
 */
void wait_for_stable_page(struct page *page)
{
	struct address_space *mapping = page_mapping(page);
	struct backing_dev_info *bdi = mapping->backing_dev_info;

	if (!bdi_cap_stable_pages_required(bdi))
		return;

	wait_on_page_writeback(page);
}
EXPORT_SYMBOL_GPL(wait_for_stable_page);
