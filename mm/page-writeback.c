/*
 * mm/page-writeback.c
 *
 * Copyright (C) 2002, Linus Torvalds.
 * Copyright (C) 2007 Red Hat, Inc., Peter Zijlstra
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
#include <linux/sched/signal.h>
#include <linux/mm_inline.h>
#include <trace/events/writeback.h>

#include "internal.h"

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

struct wb_domain global_wb_domain;

/* consolidated parameters for balance_dirty_pages() and its subroutines */
struct dirty_throttle_control {
#ifdef CONFIG_CGROUP_WRITEBACK
	struct wb_domain	*dom;
	struct dirty_throttle_control *gdtc;	/* only set in memcg dtc's */
#endif
	struct bdi_writeback	*wb;
	struct fprop_local_percpu *wb_completions;

	unsigned long		avail;		/* dirtyable */
	unsigned long		dirty;		/* file_dirty + write + nfs */
	unsigned long		thresh;		/* dirty threshold */
	unsigned long		bg_thresh;	/* dirty background threshold */

	unsigned long		wb_dirty;	/* per-wb counterparts */
	unsigned long		wb_thresh;
	unsigned long		wb_bg_thresh;

	unsigned long		pos_ratio;
};

/*
 * Length of period for aging writeout fractions of bdis. This is an
 * arbitrarily chosen number. The longer the period, the slower fractions will
 * reflect changes in current writeout rate.
 */
#define VM_COMPLETIONS_PERIOD_LEN (3*HZ)

#ifdef CONFIG_CGROUP_WRITEBACK

#define GDTC_INIT(__wb)		.wb = (__wb),				\
				.dom = &global_wb_domain,		\
				.wb_completions = &(__wb)->completions

#define GDTC_INIT_NO_WB		.dom = &global_wb_domain

#define MDTC_INIT(__wb, __gdtc)	.wb = (__wb),				\
				.dom = mem_cgroup_wb_domain(__wb),	\
				.wb_completions = &(__wb)->memcg_completions, \
				.gdtc = __gdtc

static bool mdtc_valid(struct dirty_throttle_control *dtc)
{
	return dtc->dom;
}

static struct wb_domain *dtc_dom(struct dirty_throttle_control *dtc)
{
	return dtc->dom;
}

static struct dirty_throttle_control *mdtc_gdtc(struct dirty_throttle_control *mdtc)
{
	return mdtc->gdtc;
}

static struct fprop_local_percpu *wb_memcg_completions(struct bdi_writeback *wb)
{
	return &wb->memcg_completions;
}

static void wb_min_max_ratio(struct bdi_writeback *wb,
			     unsigned long *minp, unsigned long *maxp)
{
	unsigned long this_bw = wb->avg_write_bandwidth;
	unsigned long tot_bw = atomic_long_read(&wb->bdi->tot_write_bandwidth);
	unsigned long long min = wb->bdi->min_ratio;
	unsigned long long max = wb->bdi->max_ratio;

	/*
	 * @wb may already be clean by the time control reaches here and
	 * the total may not include its bw.
	 */
	if (this_bw < tot_bw) {
		if (min) {
			min *= this_bw;
			do_div(min, tot_bw);
		}
		if (max < 100) {
			max *= this_bw;
			do_div(max, tot_bw);
		}
	}

	*minp = min;
	*maxp = max;
}

#else	/* CONFIG_CGROUP_WRITEBACK */

#define GDTC_INIT(__wb)		.wb = (__wb),                           \
				.wb_completions = &(__wb)->completions
#define GDTC_INIT_NO_WB
#define MDTC_INIT(__wb, __gdtc)

static bool mdtc_valid(struct dirty_throttle_control *dtc)
{
	return false;
}

static struct wb_domain *dtc_dom(struct dirty_throttle_control *dtc)
{
	return &global_wb_domain;
}

static struct dirty_throttle_control *mdtc_gdtc(struct dirty_throttle_control *mdtc)
{
	return NULL;
}

static struct fprop_local_percpu *wb_memcg_completions(struct bdi_writeback *wb)
{
	return NULL;
}

static void wb_min_max_ratio(struct bdi_writeback *wb,
			     unsigned long *minp, unsigned long *maxp)
{
	*minp = wb->bdi->min_ratio;
	*maxp = wb->bdi->max_ratio;
}

#endif	/* CONFIG_CGROUP_WRITEBACK */

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
 * node_dirtyable_memory - number of dirtyable pages in a node
 * @pgdat: the node
 *
 * Returns the node's number of pages potentially available for dirty
 * page cache.  This is the base value for the per-node dirty limits.
 */
static unsigned long node_dirtyable_memory(struct pglist_data *pgdat)
{
	unsigned long nr_pages = 0;
	int z;

	for (z = 0; z < MAX_NR_ZONES; z++) {
		struct zone *zone = pgdat->node_zones + z;

		if (!populated_zone(zone))
			continue;

		nr_pages += zone_page_state(zone, NR_FREE_PAGES);
	}

	/*
	 * Pages reserved for the kernel should not be considered
	 * dirtyable, to prevent a situation where reclaim has to
	 * clean pages in order to balance the zones.
	 */
	nr_pages -= min(nr_pages, pgdat->totalreserve_pages);

	nr_pages += node_page_state(pgdat, NR_INACTIVE_FILE);
	nr_pages += node_page_state(pgdat, NR_ACTIVE_FILE);

	return nr_pages;
}

static unsigned long highmem_dirtyable_memory(unsigned long total)
{
#ifdef CONFIG_HIGHMEM
	int node;
	unsigned long x = 0;
	int i;

	for_each_node_state(node, N_HIGH_MEMORY) {
		for (i = ZONE_NORMAL + 1; i < MAX_NR_ZONES; i++) {
			struct zone *z;
			unsigned long nr_pages;

			if (!is_highmem_idx(i))
				continue;

			z = &NODE_DATA(node)->node_zones[i];
			if (!populated_zone(z))
				continue;

			nr_pages = zone_page_state(z, NR_FREE_PAGES);
			/* watch for underflows */
			nr_pages -= min(nr_pages, high_wmark_pages(z));
			nr_pages += zone_page_state(z, NR_ZONE_INACTIVE_FILE);
			nr_pages += zone_page_state(z, NR_ZONE_ACTIVE_FILE);
			x += nr_pages;
		}
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
	/*
	 * Pages reserved for the kernel should not be considered
	 * dirtyable, to prevent a situation where reclaim has to
	 * clean pages in order to balance the zones.
	 */
	x -= min(x, totalreserve_pages);

	x += global_node_page_state(NR_INACTIVE_FILE);
	x += global_node_page_state(NR_ACTIVE_FILE);

	if (!vm_highmem_is_dirtyable)
		x -= highmem_dirtyable_memory(x);

	return x + 1;	/* Ensure that we never return 0 */
}

/**
 * domain_dirty_limits - calculate thresh and bg_thresh for a wb_domain
 * @dtc: dirty_throttle_control of interest
 *
 * Calculate @dtc->thresh and ->bg_thresh considering
 * vm_dirty_{bytes|ratio} and dirty_background_{bytes|ratio}.  The caller
 * must ensure that @dtc->avail is set before calling this function.  The
 * dirty limits will be lifted by 1/4 for PF_LESS_THROTTLE (ie. nfsd) and
 * real-time tasks.
 */
static void domain_dirty_limits(struct dirty_throttle_control *dtc)
{
	const unsigned long available_memory = dtc->avail;
	struct dirty_throttle_control *gdtc = mdtc_gdtc(dtc);
	unsigned long bytes = vm_dirty_bytes;
	unsigned long bg_bytes = dirty_background_bytes;
	/* convert ratios to per-PAGE_SIZE for higher precision */
	unsigned long ratio = (vm_dirty_ratio * PAGE_SIZE) / 100;
	unsigned long bg_ratio = (dirty_background_ratio * PAGE_SIZE) / 100;
	unsigned long thresh;
	unsigned long bg_thresh;
	struct task_struct *tsk;

	/* gdtc is !NULL iff @dtc is for memcg domain */
	if (gdtc) {
		unsigned long global_avail = gdtc->avail;

		/*
		 * The byte settings can't be applied directly to memcg
		 * domains.  Convert them to ratios by scaling against
		 * globally available memory.  As the ratios are in
		 * per-PAGE_SIZE, they can be obtained by dividing bytes by
		 * number of pages.
		 */
		if (bytes)
			ratio = min(DIV_ROUND_UP(bytes, global_avail),
				    PAGE_SIZE);
		if (bg_bytes)
			bg_ratio = min(DIV_ROUND_UP(bg_bytes, global_avail),
				       PAGE_SIZE);
		bytes = bg_bytes = 0;
	}

	if (bytes)
		thresh = DIV_ROUND_UP(bytes, PAGE_SIZE);
	else
		thresh = (ratio * available_memory) / PAGE_SIZE;

	if (bg_bytes)
		bg_thresh = DIV_ROUND_UP(bg_bytes, PAGE_SIZE);
	else
		bg_thresh = (bg_ratio * available_memory) / PAGE_SIZE;

	if (bg_thresh >= thresh)
		bg_thresh = thresh / 2;
	tsk = current;
	if (tsk->flags & PF_LESS_THROTTLE || rt_task(tsk)) {
		bg_thresh += bg_thresh / 4 + global_wb_domain.dirty_limit / 32;
		thresh += thresh / 4 + global_wb_domain.dirty_limit / 32;
	}
	dtc->thresh = thresh;
	dtc->bg_thresh = bg_thresh;

	/* we should eventually report the domain in the TP */
	if (!gdtc)
		trace_global_dirty_state(bg_thresh, thresh);
}

/**
 * global_dirty_limits - background-writeback and dirty-throttling thresholds
 * @pbackground: out parameter for bg_thresh
 * @pdirty: out parameter for thresh
 *
 * Calculate bg_thresh and thresh for global_wb_domain.  See
 * domain_dirty_limits() for details.
 */
void global_dirty_limits(unsigned long *pbackground, unsigned long *pdirty)
{
	struct dirty_throttle_control gdtc = { GDTC_INIT_NO_WB };

	gdtc.avail = global_dirtyable_memory();
	domain_dirty_limits(&gdtc);

	*pbackground = gdtc.bg_thresh;
	*pdirty = gdtc.thresh;
}

/**
 * node_dirty_limit - maximum number of dirty pages allowed in a node
 * @pgdat: the node
 *
 * Returns the maximum number of dirty pages allowed in a node, based
 * on the node's dirtyable memory.
 */
static unsigned long node_dirty_limit(struct pglist_data *pgdat)
{
	unsigned long node_memory = node_dirtyable_memory(pgdat);
	struct task_struct *tsk = current;
	unsigned long dirty;

	if (vm_dirty_bytes)
		dirty = DIV_ROUND_UP(vm_dirty_bytes, PAGE_SIZE) *
			node_memory / global_dirtyable_memory();
	else
		dirty = vm_dirty_ratio * node_memory / 100;

	if (tsk->flags & PF_LESS_THROTTLE || rt_task(tsk))
		dirty += dirty / 4;

	return dirty;
}

/**
 * node_dirty_ok - tells whether a node is within its dirty limits
 * @pgdat: the node to check
 *
 * Returns %true when the dirty pages in @pgdat are within the node's
 * dirty limit, %false if the limit is exceeded.
 */
bool node_dirty_ok(struct pglist_data *pgdat)
{
	unsigned long limit = node_dirty_limit(pgdat);
	unsigned long nr_pages = 0;

	nr_pages += node_page_state(pgdat, NR_FILE_DIRTY);
	nr_pages += node_page_state(pgdat, NR_UNSTABLE_NFS);
	nr_pages += node_page_state(pgdat, NR_WRITEBACK);

	return nr_pages <= limit;
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

static void wb_domain_writeout_inc(struct wb_domain *dom,
				   struct fprop_local_percpu *completions,
				   unsigned int max_prop_frac)
{
	__fprop_inc_percpu_max(&dom->completions, completions,
			       max_prop_frac);
	/* First event after period switching was turned off? */
	if (unlikely(!dom->period_time)) {
		/*
		 * We can race with other __bdi_writeout_inc calls here but
		 * it does not cause any harm since the resulting time when
		 * timer will fire and what is in writeout_period_time will be
		 * roughly the same.
		 */
		dom->period_time = wp_next_time(jiffies);
		mod_timer(&dom->period_timer, dom->period_time);
	}
}

/*
 * Increment @wb's writeout completion count and the global writeout
 * completion count. Called from test_clear_page_writeback().
 */
static inline void __wb_writeout_inc(struct bdi_writeback *wb)
{
	struct wb_domain *cgdom;

	inc_wb_stat(wb, WB_WRITTEN);
	wb_domain_writeout_inc(&global_wb_domain, &wb->completions,
			       wb->bdi->max_prop_frac);

	cgdom = mem_cgroup_wb_domain(wb);
	if (cgdom)
		wb_domain_writeout_inc(cgdom, wb_memcg_completions(wb),
				       wb->bdi->max_prop_frac);
}

void wb_writeout_inc(struct bdi_writeback *wb)
{
	unsigned long flags;

	local_irq_save(flags);
	__wb_writeout_inc(wb);
	local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(wb_writeout_inc);

/*
 * On idle system, we can be called long after we scheduled because we use
 * deferred timers so count with missed periods.
 */
static void writeout_period(unsigned long t)
{
	struct wb_domain *dom = (void *)t;
	int miss_periods = (jiffies - dom->period_time) /
						 VM_COMPLETIONS_PERIOD_LEN;

	if (fprop_new_period(&dom->completions, miss_periods + 1)) {
		dom->period_time = wp_next_time(dom->period_time +
				miss_periods * VM_COMPLETIONS_PERIOD_LEN);
		mod_timer(&dom->period_timer, dom->period_time);
	} else {
		/*
		 * Aging has zeroed all fractions. Stop wasting CPU on period
		 * updates.
		 */
		dom->period_time = 0;
	}
}

int wb_domain_init(struct wb_domain *dom, gfp_t gfp)
{
	memset(dom, 0, sizeof(*dom));

	spin_lock_init(&dom->lock);

	setup_deferrable_timer(&dom->period_timer, writeout_period,
			       (unsigned long)dom);

	dom->dirty_limit_tstamp = jiffies;

	return fprop_global_init(&dom->completions, gfp);
}

#ifdef CONFIG_CGROUP_WRITEBACK
void wb_domain_exit(struct wb_domain *dom)
{
	del_timer_sync(&dom->period_timer);
	fprop_global_destroy(&dom->completions);
}
#endif

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

static unsigned long hard_dirty_limit(struct wb_domain *dom,
				      unsigned long thresh)
{
	return max(thresh, dom->dirty_limit);
}

/*
 * Memory which can be further allocated to a memcg domain is capped by
 * system-wide clean memory excluding the amount being used in the domain.
 */
static void mdtc_calc_avail(struct dirty_throttle_control *mdtc,
			    unsigned long filepages, unsigned long headroom)
{
	struct dirty_throttle_control *gdtc = mdtc_gdtc(mdtc);
	unsigned long clean = filepages - min(filepages, mdtc->dirty);
	unsigned long global_clean = gdtc->avail - min(gdtc->avail, gdtc->dirty);
	unsigned long other_clean = global_clean - min(global_clean, clean);

	mdtc->avail = filepages + min(headroom, other_clean);
}

/**
 * __wb_calc_thresh - @wb's share of dirty throttling threshold
 * @dtc: dirty_throttle_context of interest
 *
 * Returns @wb's dirty limit in pages. The term "dirty" in the context of
 * dirty balancing includes all PG_dirty, PG_writeback and NFS unstable pages.
 *
 * Note that balance_dirty_pages() will only seriously take it as a hard limit
 * when sleeping max_pause per page is not enough to keep the dirty pages under
 * control. For example, when the device is completely stalled due to some error
 * conditions, or when there are 1000 dd tasks writing to a slow 10MB/s USB key.
 * In the other normal situations, it acts more gently by throttling the tasks
 * more (rather than completely block them) when the wb dirty pages go high.
 *
 * It allocates high/low dirty limits to fast/slow devices, in order to prevent
 * - starving fast devices
 * - piling up dirty pages (that will take long time to sync) on slow devices
 *
 * The wb's share of dirty limit will be adapting to its throughput and
 * bounded by the bdi->min_ratio and/or bdi->max_ratio parameters, if set.
 */
static unsigned long __wb_calc_thresh(struct dirty_throttle_control *dtc)
{
	struct wb_domain *dom = dtc_dom(dtc);
	unsigned long thresh = dtc->thresh;
	u64 wb_thresh;
	long numerator, denominator;
	unsigned long wb_min_ratio, wb_max_ratio;

	/*
	 * Calculate this BDI's share of the thresh ratio.
	 */
	fprop_fraction_percpu(&dom->completions, dtc->wb_completions,
			      &numerator, &denominator);

	wb_thresh = (thresh * (100 - bdi_min_ratio)) / 100;
	wb_thresh *= numerator;
	do_div(wb_thresh, denominator);

	wb_min_max_ratio(dtc->wb, &wb_min_ratio, &wb_max_ratio);

	wb_thresh += (thresh * wb_min_ratio) / 100;
	if (wb_thresh > (thresh * wb_max_ratio) / 100)
		wb_thresh = thresh * wb_max_ratio / 100;

	return wb_thresh;
}

unsigned long wb_calc_thresh(struct bdi_writeback *wb, unsigned long thresh)
{
	struct dirty_throttle_control gdtc = { GDTC_INIT(wb),
					       .thresh = thresh };
	return __wb_calc_thresh(&gdtc);
}

/*
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
static long long pos_ratio_polynom(unsigned long setpoint,
					  unsigned long dirty,
					  unsigned long limit)
{
	long long pos_ratio;
	long x;

	x = div64_s64(((s64)setpoint - (s64)dirty) << RATELIMIT_CALC_SHIFT,
		      (limit - setpoint) | 1);
	pos_ratio = x;
	pos_ratio = pos_ratio * x >> RATELIMIT_CALC_SHIFT;
	pos_ratio = pos_ratio * x >> RATELIMIT_CALC_SHIFT;
	pos_ratio += 1 << RATELIMIT_CALC_SHIFT;

	return clamp(pos_ratio, 0LL, 2LL << RATELIMIT_CALC_SHIFT);
}

/*
 * Dirty position control.
 *
 * (o) global/bdi setpoints
 *
 * We want the dirty pages be balanced around the global/wb setpoints.
 * When the number of dirty pages is higher/lower than the setpoint, the
 * dirty position control ratio (and hence task dirty ratelimit) will be
 * decreased/increased to bring the dirty pages back to the setpoint.
 *
 *     pos_ratio = 1 << RATELIMIT_CALC_SHIFT
 *
 *     if (dirty < setpoint) scale up   pos_ratio
 *     if (dirty > setpoint) scale down pos_ratio
 *
 *     if (wb_dirty < wb_setpoint) scale up   pos_ratio
 *     if (wb_dirty > wb_setpoint) scale down pos_ratio
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
 * (o) wb control line
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
 *                wb_setpoint^                    x_intercept^
 *
 * The wb control line won't drop below pos_ratio=1/4, so that wb_dirty can
 * be smoothly throttled down to normal if it starts high in situations like
 * - start writing to a slow SD card and a fast disk at the same time. The SD
 *   card's wb_dirty may rush to many times higher than wb_setpoint.
 * - the wb dirty thresh drops quickly due to change of JBOD workload
 */
static void wb_position_ratio(struct dirty_throttle_control *dtc)
{
	struct bdi_writeback *wb = dtc->wb;
	unsigned long write_bw = wb->avg_write_bandwidth;
	unsigned long freerun = dirty_freerun_ceiling(dtc->thresh, dtc->bg_thresh);
	unsigned long limit = hard_dirty_limit(dtc_dom(dtc), dtc->thresh);
	unsigned long wb_thresh = dtc->wb_thresh;
	unsigned long x_intercept;
	unsigned long setpoint;		/* dirty pages' target balance point */
	unsigned long wb_setpoint;
	unsigned long span;
	long long pos_ratio;		/* for scaling up/down the rate limit */
	long x;

	dtc->pos_ratio = 0;

	if (unlikely(dtc->dirty >= limit))
		return;

	/*
	 * global setpoint
	 *
	 * See comment for pos_ratio_polynom().
	 */
	setpoint = (freerun + limit) / 2;
	pos_ratio = pos_ratio_polynom(setpoint, dtc->dirty, limit);

	/*
	 * The strictlimit feature is a tool preventing mistrusted filesystems
	 * from growing a large number of dirty pages before throttling. For
	 * such filesystems balance_dirty_pages always checks wb counters
	 * against wb limits. Even if global "nr_dirty" is under "freerun".
	 * This is especially important for fuse which sets bdi->max_ratio to
	 * 1% by default. Without strictlimit feature, fuse writeback may
	 * consume arbitrary amount of RAM because it is accounted in
	 * NR_WRITEBACK_TEMP which is not involved in calculating "nr_dirty".
	 *
	 * Here, in wb_position_ratio(), we calculate pos_ratio based on
	 * two values: wb_dirty and wb_thresh. Let's consider an example:
	 * total amount of RAM is 16GB, bdi->max_ratio is equal to 1%, global
	 * limits are set by default to 10% and 20% (background and throttle).
	 * Then wb_thresh is 1% of 20% of 16GB. This amounts to ~8K pages.
	 * wb_calc_thresh(wb, bg_thresh) is about ~4K pages. wb_setpoint is
	 * about ~6K pages (as the average of background and throttle wb
	 * limits). The 3rd order polynomial will provide positive feedback if
	 * wb_dirty is under wb_setpoint and vice versa.
	 *
	 * Note, that we cannot use global counters in these calculations
	 * because we want to throttle process writing to a strictlimit wb
	 * much earlier than global "freerun" is reached (~23MB vs. ~2.3GB
	 * in the example above).
	 */
	if (unlikely(wb->bdi->capabilities & BDI_CAP_STRICTLIMIT)) {
		long long wb_pos_ratio;

		if (dtc->wb_dirty < 8) {
			dtc->pos_ratio = min_t(long long, pos_ratio * 2,
					   2 << RATELIMIT_CALC_SHIFT);
			return;
		}

		if (dtc->wb_dirty >= wb_thresh)
			return;

		wb_setpoint = dirty_freerun_ceiling(wb_thresh,
						    dtc->wb_bg_thresh);

		if (wb_setpoint == 0 || wb_setpoint == wb_thresh)
			return;

		wb_pos_ratio = pos_ratio_polynom(wb_setpoint, dtc->wb_dirty,
						 wb_thresh);

		/*
		 * Typically, for strictlimit case, wb_setpoint << setpoint
		 * and pos_ratio >> wb_pos_ratio. In the other words global
		 * state ("dirty") is not limiting factor and we have to
		 * make decision based on wb counters. But there is an
		 * important case when global pos_ratio should get precedence:
		 * global limits are exceeded (e.g. due to activities on other
		 * wb's) while given strictlimit wb is below limit.
		 *
		 * "pos_ratio * wb_pos_ratio" would work for the case above,
		 * but it would look too non-natural for the case of all
		 * activity in the system coming from a single strictlimit wb
		 * with bdi->max_ratio == 100%.
		 *
		 * Note that min() below somewhat changes the dynamics of the
		 * control system. Normally, pos_ratio value can be well over 3
		 * (when globally we are at freerun and wb is well below wb
		 * setpoint). Now the maximum pos_ratio in the same situation
		 * is 2. We might want to tweak this if we observe the control
		 * system is too slow to adapt.
		 */
		dtc->pos_ratio = min(pos_ratio, wb_pos_ratio);
		return;
	}

	/*
	 * We have computed basic pos_ratio above based on global situation. If
	 * the wb is over/under its share of dirty pages, we want to scale
	 * pos_ratio further down/up. That is done by the following mechanism.
	 */

	/*
	 * wb setpoint
	 *
	 *        f(wb_dirty) := 1.0 + k * (wb_dirty - wb_setpoint)
	 *
	 *                        x_intercept - wb_dirty
	 *                     := --------------------------
	 *                        x_intercept - wb_setpoint
	 *
	 * The main wb control line is a linear function that subjects to
	 *
	 * (1) f(wb_setpoint) = 1.0
	 * (2) k = - 1 / (8 * write_bw)  (in single wb case)
	 *     or equally: x_intercept = wb_setpoint + 8 * write_bw
	 *
	 * For single wb case, the dirty pages are observed to fluctuate
	 * regularly within range
	 *        [wb_setpoint - write_bw/2, wb_setpoint + write_bw/2]
	 * for various filesystems, where (2) can yield in a reasonable 12.5%
	 * fluctuation range for pos_ratio.
	 *
	 * For JBOD case, wb_thresh (not wb_dirty!) could fluctuate up to its
	 * own size, so move the slope over accordingly and choose a slope that
	 * yields 100% pos_ratio fluctuation on suddenly doubled wb_thresh.
	 */
	if (unlikely(wb_thresh > dtc->thresh))
		wb_thresh = dtc->thresh;
	/*
	 * It's very possible that wb_thresh is close to 0 not because the
	 * device is slow, but that it has remained inactive for long time.
	 * Honour such devices a reasonable good (hopefully IO efficient)
	 * threshold, so that the occasional writes won't be blocked and active
	 * writes can rampup the threshold quickly.
	 */
	wb_thresh = max(wb_thresh, (limit - dtc->dirty) / 8);
	/*
	 * scale global setpoint to wb's:
	 *	wb_setpoint = setpoint * wb_thresh / thresh
	 */
	x = div_u64((u64)wb_thresh << 16, dtc->thresh | 1);
	wb_setpoint = setpoint * (u64)x >> 16;
	/*
	 * Use span=(8*write_bw) in single wb case as indicated by
	 * (thresh - wb_thresh ~= 0) and transit to wb_thresh in JBOD case.
	 *
	 *        wb_thresh                    thresh - wb_thresh
	 * span = --------- * (8 * write_bw) + ------------------ * wb_thresh
	 *         thresh                           thresh
	 */
	span = (dtc->thresh - wb_thresh + 8 * write_bw) * (u64)x >> 16;
	x_intercept = wb_setpoint + span;

	if (dtc->wb_dirty < x_intercept - span / 4) {
		pos_ratio = div64_u64(pos_ratio * (x_intercept - dtc->wb_dirty),
				      (x_intercept - wb_setpoint) | 1);
	} else
		pos_ratio /= 4;

	/*
	 * wb reserve area, safeguard against dirty pool underrun and disk idle
	 * It may push the desired control point of global dirty pages higher
	 * than setpoint.
	 */
	x_intercept = wb_thresh / 2;
	if (dtc->wb_dirty < x_intercept) {
		if (dtc->wb_dirty > x_intercept / 8)
			pos_ratio = div_u64(pos_ratio * x_intercept,
					    dtc->wb_dirty);
		else
			pos_ratio *= 8;
	}

	dtc->pos_ratio = pos_ratio;
}

static void wb_update_write_bandwidth(struct bdi_writeback *wb,
				      unsigned long elapsed,
				      unsigned long written)
{
	const unsigned long period = roundup_pow_of_two(3 * HZ);
	unsigned long avg = wb->avg_write_bandwidth;
	unsigned long old = wb->write_bandwidth;
	u64 bw;

	/*
	 * bw = written * HZ / elapsed
	 *
	 *                   bw * elapsed + write_bandwidth * (period - elapsed)
	 * write_bandwidth = ---------------------------------------------------
	 *                                          period
	 *
	 * @written may have decreased due to account_page_redirty().
	 * Avoid underflowing @bw calculation.
	 */
	bw = written - min(written, wb->written_stamp);
	bw *= HZ;
	if (unlikely(elapsed > period)) {
		do_div(bw, elapsed);
		avg = bw;
		goto out;
	}
	bw += (u64)wb->write_bandwidth * (period - elapsed);
	bw >>= ilog2(period);

	/*
	 * one more level of smoothing, for filtering out sudden spikes
	 */
	if (avg > old && old >= (unsigned long)bw)
		avg -= (avg - old) >> 3;

	if (avg < old && old <= (unsigned long)bw)
		avg += (old - avg) >> 3;

out:
	/* keep avg > 0 to guarantee that tot > 0 if there are dirty wbs */
	avg = max(avg, 1LU);
	if (wb_has_dirty_io(wb)) {
		long delta = avg - wb->avg_write_bandwidth;
		WARN_ON_ONCE(atomic_long_add_return(delta,
					&wb->bdi->tot_write_bandwidth) <= 0);
	}
	wb->write_bandwidth = bw;
	wb->avg_write_bandwidth = avg;
}

static void update_dirty_limit(struct dirty_throttle_control *dtc)
{
	struct wb_domain *dom = dtc_dom(dtc);
	unsigned long thresh = dtc->thresh;
	unsigned long limit = dom->dirty_limit;

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
	 * dom->dirty_limit which is guaranteed to lie above the dirty pages.
	 */
	thresh = max(thresh, dtc->dirty);
	if (limit > thresh) {
		limit -= (limit - thresh) >> 5;
		goto update;
	}
	return;
update:
	dom->dirty_limit = limit;
}

static void domain_update_bandwidth(struct dirty_throttle_control *dtc,
				    unsigned long now)
{
	struct wb_domain *dom = dtc_dom(dtc);

	/*
	 * check locklessly first to optimize away locking for the most time
	 */
	if (time_before(now, dom->dirty_limit_tstamp + BANDWIDTH_INTERVAL))
		return;

	spin_lock(&dom->lock);
	if (time_after_eq(now, dom->dirty_limit_tstamp + BANDWIDTH_INTERVAL)) {
		update_dirty_limit(dtc);
		dom->dirty_limit_tstamp = now;
	}
	spin_unlock(&dom->lock);
}

/*
 * Maintain wb->dirty_ratelimit, the base dirty throttle rate.
 *
 * Normal wb tasks will be curbed at or below it in long term.
 * Obviously it should be around (write_bw / N) when there are N dd tasks.
 */
static void wb_update_dirty_ratelimit(struct dirty_throttle_control *dtc,
				      unsigned long dirtied,
				      unsigned long elapsed)
{
	struct bdi_writeback *wb = dtc->wb;
	unsigned long dirty = dtc->dirty;
	unsigned long freerun = dirty_freerun_ceiling(dtc->thresh, dtc->bg_thresh);
	unsigned long limit = hard_dirty_limit(dtc_dom(dtc), dtc->thresh);
	unsigned long setpoint = (freerun + limit) / 2;
	unsigned long write_bw = wb->avg_write_bandwidth;
	unsigned long dirty_ratelimit = wb->dirty_ratelimit;
	unsigned long dirty_rate;
	unsigned long task_ratelimit;
	unsigned long balanced_dirty_ratelimit;
	unsigned long step;
	unsigned long x;
	unsigned long shift;

	/*
	 * The dirty rate will match the writeout rate in long term, except
	 * when dirty pages are truncated by userspace or re-dirtied by FS.
	 */
	dirty_rate = (dirtied - wb->dirtied_stamp) * HZ / elapsed;

	/*
	 * task_ratelimit reflects each dd's dirty rate for the past 200ms.
	 */
	task_ratelimit = (u64)dirty_ratelimit *
					dtc->pos_ratio >> RATELIMIT_CALC_SHIFT;
	task_ratelimit++; /* it helps rampup dirty_ratelimit from tiny values */

	/*
	 * A linear estimation of the "balanced" throttle rate. The theory is,
	 * if there are N dd tasks, each throttled at task_ratelimit, the wb's
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
	 *	wb->dirty_ratelimit = balanced_dirty_ratelimit;
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

	/*
	 * For strictlimit case, calculations above were based on wb counters
	 * and limits (starting from pos_ratio = wb_position_ratio() and up to
	 * balanced_dirty_ratelimit = task_ratelimit * write_bw / dirty_rate).
	 * Hence, to calculate "step" properly, we have to use wb_dirty as
	 * "dirty" and wb_setpoint as "setpoint".
	 *
	 * We rampup dirty_ratelimit forcibly if wb_dirty is low because
	 * it's possible that wb_thresh is close to zero due to inactivity
	 * of backing device.
	 */
	if (unlikely(wb->bdi->capabilities & BDI_CAP_STRICTLIMIT)) {
		dirty = dtc->wb_dirty;
		if (dtc->wb_dirty < 8)
			setpoint = dtc->wb_dirty + 1;
		else
			setpoint = (dtc->wb_thresh + dtc->wb_bg_thresh) / 2;
	}

	if (dirty < setpoint) {
		x = min3(wb->balanced_dirty_ratelimit,
			 balanced_dirty_ratelimit, task_ratelimit);
		if (dirty_ratelimit < x)
			step = x - dirty_ratelimit;
	} else {
		x = max3(wb->balanced_dirty_ratelimit,
			 balanced_dirty_ratelimit, task_ratelimit);
		if (dirty_ratelimit > x)
			step = dirty_ratelimit - x;
	}

	/*
	 * Don't pursue 100% rate matching. It's impossible since the balanced
	 * rate itself is constantly fluctuating. So decrease the track speed
	 * when it gets close to the target. Helps eliminate pointless tremors.
	 */
	shift = dirty_ratelimit / (2 * step + 1);
	if (shift < BITS_PER_LONG)
		step = DIV_ROUND_UP(step >> shift, 8);
	else
		step = 0;

	if (dirty_ratelimit < balanced_dirty_ratelimit)
		dirty_ratelimit += step;
	else
		dirty_ratelimit -= step;

	wb->dirty_ratelimit = max(dirty_ratelimit, 1UL);
	wb->balanced_dirty_ratelimit = balanced_dirty_ratelimit;

	trace_bdi_dirty_ratelimit(wb, dirty_rate, task_ratelimit);
}

static void __wb_update_bandwidth(struct dirty_throttle_control *gdtc,
				  struct dirty_throttle_control *mdtc,
				  unsigned long start_time,
				  bool update_ratelimit)
{
	struct bdi_writeback *wb = gdtc->wb;
	unsigned long now = jiffies;
	unsigned long elapsed = now - wb->bw_time_stamp;
	unsigned long dirtied;
	unsigned long written;

	lockdep_assert_held(&wb->list_lock);

	/*
	 * rate-limit, only update once every 200ms.
	 */
	if (elapsed < BANDWIDTH_INTERVAL)
		return;

	dirtied = percpu_counter_read(&wb->stat[WB_DIRTIED]);
	written = percpu_counter_read(&wb->stat[WB_WRITTEN]);

	/*
	 * Skip quiet periods when disk bandwidth is under-utilized.
	 * (at least 1s idle time between two flusher runs)
	 */
	if (elapsed > HZ && time_before(wb->bw_time_stamp, start_time))
		goto snapshot;

	if (update_ratelimit) {
		domain_update_bandwidth(gdtc, now);
		wb_update_dirty_ratelimit(gdtc, dirtied, elapsed);

		/*
		 * @mdtc is always NULL if !CGROUP_WRITEBACK but the
		 * compiler has no way to figure that out.  Help it.
		 */
		if (IS_ENABLED(CONFIG_CGROUP_WRITEBACK) && mdtc) {
			domain_update_bandwidth(mdtc, now);
			wb_update_dirty_ratelimit(mdtc, dirtied, elapsed);
		}
	}
	wb_update_write_bandwidth(wb, elapsed, written);

snapshot:
	wb->dirtied_stamp = dirtied;
	wb->written_stamp = written;
	wb->bw_time_stamp = now;
}

void wb_update_bandwidth(struct bdi_writeback *wb, unsigned long start_time)
{
	struct dirty_throttle_control gdtc = { GDTC_INIT(wb) };

	__wb_update_bandwidth(&gdtc, NULL, start_time, false);
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

static unsigned long wb_max_pause(struct bdi_writeback *wb,
				  unsigned long wb_dirty)
{
	unsigned long bw = wb->avg_write_bandwidth;
	unsigned long t;

	/*
	 * Limit pause time for small memory systems. If sleeping for too long
	 * time, a small pool of dirty/writeback pages may go empty and disk go
	 * idle.
	 *
	 * 8 serves as the safety ratio.
	 */
	t = wb_dirty / (1 + bw / roundup_pow_of_two(1 + HZ / 8));
	t++;

	return min_t(unsigned long, t, MAX_PAUSE);
}

static long wb_min_pause(struct bdi_writeback *wb,
			 long max_pause,
			 unsigned long task_ratelimit,
			 unsigned long dirty_ratelimit,
			 int *nr_dirtied_pause)
{
	long hi = ilog2(wb->avg_write_bandwidth);
	long lo = ilog2(wb->dirty_ratelimit);
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

static inline void wb_dirty_limits(struct dirty_throttle_control *dtc)
{
	struct bdi_writeback *wb = dtc->wb;
	unsigned long wb_reclaimable;

	/*
	 * wb_thresh is not treated as some limiting factor as
	 * dirty_thresh, due to reasons
	 * - in JBOD setup, wb_thresh can fluctuate a lot
	 * - in a system with HDD and USB key, the USB key may somehow
	 *   go into state (wb_dirty >> wb_thresh) either because
	 *   wb_dirty starts high, or because wb_thresh drops low.
	 *   In this case we don't want to hard throttle the USB key
	 *   dirtiers for 100 seconds until wb_dirty drops under
	 *   wb_thresh. Instead the auxiliary wb control line in
	 *   wb_position_ratio() will let the dirtier task progress
	 *   at some rate <= (write_bw / 2) for bringing down wb_dirty.
	 */
	dtc->wb_thresh = __wb_calc_thresh(dtc);
	dtc->wb_bg_thresh = dtc->thresh ?
		div_u64((u64)dtc->wb_thresh * dtc->bg_thresh, dtc->thresh) : 0;

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
	if (dtc->wb_thresh < 2 * wb_stat_error(wb)) {
		wb_reclaimable = wb_stat_sum(wb, WB_RECLAIMABLE);
		dtc->wb_dirty = wb_reclaimable + wb_stat_sum(wb, WB_WRITEBACK);
	} else {
		wb_reclaimable = wb_stat(wb, WB_RECLAIMABLE);
		dtc->wb_dirty = wb_reclaimable + wb_stat(wb, WB_WRITEBACK);
	}
}

/*
 * balance_dirty_pages() must be called by processes which are generating dirty
 * data.  It looks at the number of dirty pages in the machine and will force
 * the caller to wait once crossing the (background_thresh + dirty_thresh) / 2.
 * If we're over `background_thresh' then the writeback threads are woken to
 * perform some writeout.
 */
static void balance_dirty_pages(struct address_space *mapping,
				struct bdi_writeback *wb,
				unsigned long pages_dirtied)
{
	struct dirty_throttle_control gdtc_stor = { GDTC_INIT(wb) };
	struct dirty_throttle_control mdtc_stor = { MDTC_INIT(wb, &gdtc_stor) };
	struct dirty_throttle_control * const gdtc = &gdtc_stor;
	struct dirty_throttle_control * const mdtc = mdtc_valid(&mdtc_stor) ?
						     &mdtc_stor : NULL;
	struct dirty_throttle_control *sdtc;
	unsigned long nr_reclaimable;	/* = file_dirty + unstable_nfs */
	long period;
	long pause;
	long max_pause;
	long min_pause;
	int nr_dirtied_pause;
	bool dirty_exceeded = false;
	unsigned long task_ratelimit;
	unsigned long dirty_ratelimit;
	struct backing_dev_info *bdi = wb->bdi;
	bool strictlimit = bdi->capabilities & BDI_CAP_STRICTLIMIT;
	unsigned long start_time = jiffies;

	for (;;) {
		unsigned long now = jiffies;
		unsigned long dirty, thresh, bg_thresh;
		unsigned long m_dirty = 0;	/* stop bogus uninit warnings */
		unsigned long m_thresh = 0;
		unsigned long m_bg_thresh = 0;

		/*
		 * Unstable writes are a feature of certain networked
		 * filesystems (i.e. NFS) in which data may have been
		 * written to the server's write cache, but has not yet
		 * been flushed to permanent storage.
		 */
		nr_reclaimable = global_node_page_state(NR_FILE_DIRTY) +
					global_node_page_state(NR_UNSTABLE_NFS);
		gdtc->avail = global_dirtyable_memory();
		gdtc->dirty = nr_reclaimable + global_node_page_state(NR_WRITEBACK);

		domain_dirty_limits(gdtc);

		if (unlikely(strictlimit)) {
			wb_dirty_limits(gdtc);

			dirty = gdtc->wb_dirty;
			thresh = gdtc->wb_thresh;
			bg_thresh = gdtc->wb_bg_thresh;
		} else {
			dirty = gdtc->dirty;
			thresh = gdtc->thresh;
			bg_thresh = gdtc->bg_thresh;
		}

		if (mdtc) {
			unsigned long filepages, headroom, writeback;

			/*
			 * If @wb belongs to !root memcg, repeat the same
			 * basic calculations for the memcg domain.
			 */
			mem_cgroup_wb_stats(wb, &filepages, &headroom,
					    &mdtc->dirty, &writeback);
			mdtc->dirty += writeback;
			mdtc_calc_avail(mdtc, filepages, headroom);

			domain_dirty_limits(mdtc);

			if (unlikely(strictlimit)) {
				wb_dirty_limits(mdtc);
				m_dirty = mdtc->wb_dirty;
				m_thresh = mdtc->wb_thresh;
				m_bg_thresh = mdtc->wb_bg_thresh;
			} else {
				m_dirty = mdtc->dirty;
				m_thresh = mdtc->thresh;
				m_bg_thresh = mdtc->bg_thresh;
			}
		}

		/*
		 * Throttle it only when the background writeback cannot
		 * catch-up. This avoids (excessively) small writeouts
		 * when the wb limits are ramping up in case of !strictlimit.
		 *
		 * In strictlimit case make decision based on the wb counters
		 * and limits. Small writeouts when the wb limits are ramping
		 * up are the price we consciously pay for strictlimit-ing.
		 *
		 * If memcg domain is in effect, @dirty should be under
		 * both global and memcg freerun ceilings.
		 */
		if (dirty <= dirty_freerun_ceiling(thresh, bg_thresh) &&
		    (!mdtc ||
		     m_dirty <= dirty_freerun_ceiling(m_thresh, m_bg_thresh))) {
			unsigned long intv = dirty_poll_interval(dirty, thresh);
			unsigned long m_intv = ULONG_MAX;

			current->dirty_paused_when = now;
			current->nr_dirtied = 0;
			if (mdtc)
				m_intv = dirty_poll_interval(m_dirty, m_thresh);
			current->nr_dirtied_pause = min(intv, m_intv);
			break;
		}

		if (unlikely(!writeback_in_progress(wb)))
			wb_start_background_writeback(wb);

		/*
		 * Calculate global domain's pos_ratio and select the
		 * global dtc by default.
		 */
		if (!strictlimit)
			wb_dirty_limits(gdtc);

		dirty_exceeded = (gdtc->wb_dirty > gdtc->wb_thresh) &&
			((gdtc->dirty > gdtc->thresh) || strictlimit);

		wb_position_ratio(gdtc);
		sdtc = gdtc;

		if (mdtc) {
			/*
			 * If memcg domain is in effect, calculate its
			 * pos_ratio.  @wb should satisfy constraints from
			 * both global and memcg domains.  Choose the one
			 * w/ lower pos_ratio.
			 */
			if (!strictlimit)
				wb_dirty_limits(mdtc);

			dirty_exceeded |= (mdtc->wb_dirty > mdtc->wb_thresh) &&
				((mdtc->dirty > mdtc->thresh) || strictlimit);

			wb_position_ratio(mdtc);
			if (mdtc->pos_ratio < gdtc->pos_ratio)
				sdtc = mdtc;
		}

		if (dirty_exceeded && !wb->dirty_exceeded)
			wb->dirty_exceeded = 1;

		if (time_is_before_jiffies(wb->bw_time_stamp +
					   BANDWIDTH_INTERVAL)) {
			spin_lock(&wb->list_lock);
			__wb_update_bandwidth(gdtc, mdtc, start_time, true);
			spin_unlock(&wb->list_lock);
		}

		/* throttle according to the chosen dtc */
		dirty_ratelimit = wb->dirty_ratelimit;
		task_ratelimit = ((u64)dirty_ratelimit * sdtc->pos_ratio) >>
							RATELIMIT_CALC_SHIFT;
		max_pause = wb_max_pause(wb, sdtc->wb_dirty);
		min_pause = wb_min_pause(wb, max_pause,
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
			trace_balance_dirty_pages(wb,
						  sdtc->thresh,
						  sdtc->bg_thresh,
						  sdtc->dirty,
						  sdtc->wb_thresh,
						  sdtc->wb_dirty,
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
		trace_balance_dirty_pages(wb,
					  sdtc->thresh,
					  sdtc->bg_thresh,
					  sdtc->dirty,
					  sdtc->wb_thresh,
					  sdtc->wb_dirty,
					  dirty_ratelimit,
					  task_ratelimit,
					  pages_dirtied,
					  period,
					  pause,
					  start_time);
		__set_current_state(TASK_KILLABLE);
		wb->dirty_sleep = now;
		io_schedule_timeout(pause);

		current->dirty_paused_when = now + pause;
		current->nr_dirtied = 0;
		current->nr_dirtied_pause = nr_dirtied_pause;

		/*
		 * This is typically equal to (dirty < thresh) and can also
		 * keep "1000+ dd on a slow USB stick" under control.
		 */
		if (task_ratelimit)
			break;

		/*
		 * In the case of an unresponding NFS server and the NFS dirty
		 * pages exceeds dirty_thresh, give the other good wb's a pipe
		 * to go through, so that tasks on them still remain responsive.
		 *
		 * In theory 1 page is enough to keep the consumer-producer
		 * pipe going: the flusher cleans 1 page => the task dirties 1
		 * more page. However wb_dirty has accounting errors.  So use
		 * the larger and more IO friendly wb_stat_error.
		 */
		if (sdtc->wb_dirty <= wb_stat_error(wb))
			break;

		if (fatal_signal_pending(current))
			break;
	}

	if (!dirty_exceeded && wb->dirty_exceeded)
		wb->dirty_exceeded = 0;

	if (writeback_in_progress(wb))
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

	if (nr_reclaimable > gdtc->bg_thresh)
		wb_start_background_writeback(wb);
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
	struct inode *inode = mapping->host;
	struct backing_dev_info *bdi = inode_to_bdi(inode);
	struct bdi_writeback *wb = NULL;
	int ratelimit;
	int *p;

	if (!bdi_cap_account_dirty(bdi))
		return;

	if (inode_cgwb_enabled(inode))
		wb = wb_get_create_current(bdi, GFP_KERNEL);
	if (!wb)
		wb = &bdi->wb;

	ratelimit = current->nr_dirtied_pause;
	if (wb->dirty_exceeded)
		ratelimit = min(ratelimit, 32 >> (PAGE_SHIFT - 10));

	preempt_disable();
	/*
	 * This prevents one CPU to accumulate too many dirtied pages without
	 * calling into balance_dirty_pages(), which can happen when there are
	 * 1000+ tasks, all of them start dirtying pages at exactly the same
	 * time, hence all honoured too large initial task->nr_dirtied_pause.
	 */
	p =  this_cpu_ptr(&bdp_ratelimits);
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
	p = this_cpu_ptr(&dirty_throttle_leaks);
	if (*p > 0 && current->nr_dirtied < ratelimit) {
		unsigned long nr_pages_dirtied;
		nr_pages_dirtied = min(*p, ratelimit - current->nr_dirtied);
		*p -= nr_pages_dirtied;
		current->nr_dirtied += nr_pages_dirtied;
	}
	preempt_enable();

	if (unlikely(current->nr_dirtied >= ratelimit))
		balance_dirty_pages(mapping, wb, current->nr_dirtied);

	wb_put(wb);
}
EXPORT_SYMBOL(balance_dirty_pages_ratelimited);

/**
 * wb_over_bg_thresh - does @wb need to be written back?
 * @wb: bdi_writeback of interest
 *
 * Determines whether background writeback should keep writing @wb or it's
 * clean enough.  Returns %true if writeback should continue.
 */
bool wb_over_bg_thresh(struct bdi_writeback *wb)
{
	struct dirty_throttle_control gdtc_stor = { GDTC_INIT(wb) };
	struct dirty_throttle_control mdtc_stor = { MDTC_INIT(wb, &gdtc_stor) };
	struct dirty_throttle_control * const gdtc = &gdtc_stor;
	struct dirty_throttle_control * const mdtc = mdtc_valid(&mdtc_stor) ?
						     &mdtc_stor : NULL;

	/*
	 * Similar to balance_dirty_pages() but ignores pages being written
	 * as we're trying to decide whether to put more under writeback.
	 */
	gdtc->avail = global_dirtyable_memory();
	gdtc->dirty = global_node_page_state(NR_FILE_DIRTY) +
		      global_node_page_state(NR_UNSTABLE_NFS);
	domain_dirty_limits(gdtc);

	if (gdtc->dirty > gdtc->bg_thresh)
		return true;

	if (wb_stat(wb, WB_RECLAIMABLE) >
	    wb_calc_thresh(gdtc->wb, gdtc->bg_thresh))
		return true;

	if (mdtc) {
		unsigned long filepages, headroom, writeback;

		mem_cgroup_wb_stats(wb, &filepages, &headroom, &mdtc->dirty,
				    &writeback);
		mdtc_calc_avail(mdtc, filepages, headroom);
		domain_dirty_limits(mdtc);	/* ditto, ignore writeback */

		if (mdtc->dirty > mdtc->bg_thresh)
			return true;

		if (wb_stat(wb, WB_RECLAIMABLE) >
		    wb_calc_thresh(mdtc->wb, mdtc->bg_thresh))
			return true;
	}

	return false;
}

/*
 * sysctl handler for /proc/sys/vm/dirty_writeback_centisecs
 */
int dirty_writeback_centisecs_handler(struct ctl_table *table, int write,
	void __user *buffer, size_t *length, loff_t *ppos)
{
	proc_dointvec(table, write, buffer, length, ppos);
	return 0;
}

#ifdef CONFIG_BLOCK
void laptop_mode_timer_fn(unsigned long data)
{
	struct request_queue *q = (struct request_queue *)data;
	int nr_pages = global_node_page_state(NR_FILE_DIRTY) +
		global_node_page_state(NR_UNSTABLE_NFS);
	struct bdi_writeback *wb;

	/*
	 * We want to write everything out, not just down to the dirty
	 * threshold
	 */
	if (!bdi_has_dirty_io(q->backing_dev_info))
		return;

	rcu_read_lock();
	list_for_each_entry_rcu(wb, &q->backing_dev_info->wb_list, bdi_node)
		if (wb_has_dirty_io(wb))
			wb_start_writeback(wb, nr_pages, true,
					   WB_REASON_LAPTOP_TIMER);
	rcu_read_unlock();
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
	struct wb_domain *dom = &global_wb_domain;
	unsigned long background_thresh;
	unsigned long dirty_thresh;

	global_dirty_limits(&background_thresh, &dirty_thresh);
	dom->dirty_limit = dirty_thresh;
	ratelimit_pages = dirty_thresh / (num_online_cpus() * 32);
	if (ratelimit_pages < 16)
		ratelimit_pages = 16;
}

static int page_writeback_cpu_online(unsigned int cpu)
{
	writeback_set_ratelimit();
	return 0;
}

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
	BUG_ON(wb_domain_init(&global_wb_domain, GFP_KERNEL));

	cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "mm/writeback:online",
			  page_writeback_cpu_online, NULL);
	cpuhp_setup_state(CPUHP_MM_WRITEBACK_DEAD, "mm/writeback:dead", NULL,
			  page_writeback_cpu_online);
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
	unsigned long tagged = 0;
	struct radix_tree_iter iter;
	void **slot;

	spin_lock_irq(&mapping->tree_lock);
	radix_tree_for_each_tagged(slot, &mapping->page_tree, &iter, start,
							PAGECACHE_TAG_DIRTY) {
		if (iter.index > end)
			break;
		radix_tree_iter_tag_set(&mapping->page_tree, &iter,
							PAGECACHE_TAG_TOWRITE);
		tagged++;
		if ((tagged % WRITEBACK_TAG_BATCH) != 0)
			continue;
		slot = radix_tree_iter_resume(slot, &iter);
		spin_unlock_irq(&mapping->tree_lock);
		cond_resched();
		spin_lock_irq(&mapping->tree_lock);
	}
	spin_unlock_irq(&mapping->tree_lock);
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
		index = wbc->range_start >> PAGE_SHIFT;
		end = wbc->range_end >> PAGE_SHIFT;
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

			trace_wbc_writepage(wbc, inode_to_bdi(mapping->host));
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
	while (1) {
		if (mapping->a_ops->writepages)
			ret = mapping->a_ops->writepages(mapping, wbc);
		else
			ret = generic_writepages(mapping, wbc);
		if ((ret != -ENOMEM) || (wbc->sync_mode != WB_SYNC_ALL))
			break;
		cond_resched();
		congestion_wait(BLK_RW_ASYNC, HZ/50);
	}
	return ret;
}

/**
 * write_one_page - write out a single page and wait on I/O
 * @page: the page to write
 *
 * The page must be locked by the caller and will be unlocked upon return.
 *
 * Note that the mapping's AS_EIO/AS_ENOSPC flags will be cleared when this
 * function returns.
 */
int write_one_page(struct page *page)
{
	struct address_space *mapping = page->mapping;
	int ret = 0;
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_ALL,
		.nr_to_write = 1,
	};

	BUG_ON(!PageLocked(page));

	wait_on_page_writeback(page);

	if (clear_page_dirty_for_io(page)) {
		get_page(page);
		ret = mapping->a_ops->writepage(page, &wbc);
		if (ret == 0)
			wait_on_page_writeback(page);
		put_page(page);
	} else {
		unlock_page(page);
	}

	if (!ret)
		ret = filemap_check_errors(mapping);
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
 *
 * Caller must hold lock_page_memcg().
 *
 * NOTE: This relies on being atomic wrt interrupts.
 */
void account_page_dirtied(struct page *page, struct address_space *mapping)
{
	struct inode *inode = mapping->host;

	trace_writeback_dirty_page(page, mapping);

	if (mapping_cap_account_dirty(mapping)) {
		struct bdi_writeback *wb;

		inode_attach_wb(inode, page);
		wb = inode_to_wb(inode);

		__inc_lruvec_page_state(page, NR_FILE_DIRTY);
		__inc_zone_page_state(page, NR_ZONE_WRITE_PENDING);
		__inc_node_page_state(page, NR_DIRTIED);
		inc_wb_stat(wb, WB_RECLAIMABLE);
		inc_wb_stat(wb, WB_DIRTIED);
		task_io_account_write(PAGE_SIZE);
		current->nr_dirtied++;
		this_cpu_inc(bdp_ratelimits);
	}
}
EXPORT_SYMBOL(account_page_dirtied);

/*
 * Helper function for deaccounting dirty page without writeback.
 *
 * Caller must hold lock_page_memcg().
 */
void account_page_cleaned(struct page *page, struct address_space *mapping,
			  struct bdi_writeback *wb)
{
	if (mapping_cap_account_dirty(mapping)) {
		dec_lruvec_page_state(page, NR_FILE_DIRTY);
		dec_zone_page_state(page, NR_ZONE_WRITE_PENDING);
		dec_wb_stat(wb, WB_RECLAIMABLE);
		task_io_account_cancelled_write(PAGE_SIZE);
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
 * The caller must ensure this doesn't race with truncation.  Most will simply
 * hold the page lock, but e.g. zap_pte_range() calls with the page mapped and
 * the pte lock held, which also locks out truncation.
 */
int __set_page_dirty_nobuffers(struct page *page)
{
	lock_page_memcg(page);
	if (!TestSetPageDirty(page)) {
		struct address_space *mapping = page_mapping(page);
		unsigned long flags;

		if (!mapping) {
			unlock_page_memcg(page);
			return 1;
		}

		spin_lock_irqsave(&mapping->tree_lock, flags);
		BUG_ON(page_mapping(page) != mapping);
		WARN_ON_ONCE(!PagePrivate(page) && !PageUptodate(page));
		account_page_dirtied(page, mapping);
		radix_tree_tag_set(&mapping->page_tree, page_index(page),
				   PAGECACHE_TAG_DIRTY);
		spin_unlock_irqrestore(&mapping->tree_lock, flags);
		unlock_page_memcg(page);

		if (mapping->host) {
			/* !PageAnon && !swapper_space */
			__mark_inode_dirty(mapping->host, I_DIRTY_PAGES);
		}
		return 1;
	}
	unlock_page_memcg(page);
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
		struct inode *inode = mapping->host;
		struct bdi_writeback *wb;
		bool locked;

		wb = unlocked_inode_to_wb_begin(inode, &locked);
		current->nr_dirtied--;
		dec_node_page_state(page, NR_DIRTIED);
		dec_wb_stat(wb, WB_DIRTIED);
		unlocked_inode_to_wb_end(inode, locked);
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
	int ret;

	wbc->pages_skipped++;
	ret = __set_page_dirty_nobuffers(page);
	account_page_redirty(page);
	return ret;
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

	page = compound_head(page);
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
		if (PageReclaim(page))
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
 * This cancels just the dirty bit on the kernel page itself, it does NOT
 * actually remove dirty bits on any mmap's that may be around. It also
 * leaves the page tagged dirty, so any sync activity will still find it on
 * the dirty lists, and in particular, clear_page_dirty_for_io() will still
 * look at the dirty bits in the VM.
 *
 * Doing this should *normally* only ever be done when a page is truncated,
 * and is not actually mapped anywhere at all. However, fs/buffer.c does
 * this when it notices that somebody has cleaned out all the buffers on a
 * page without actually doing it through the VM. Can you say "ext3 is
 * horribly ugly"? Thought you could.
 */
void cancel_dirty_page(struct page *page)
{
	struct address_space *mapping = page_mapping(page);

	if (mapping_cap_account_dirty(mapping)) {
		struct inode *inode = mapping->host;
		struct bdi_writeback *wb;
		bool locked;

		lock_page_memcg(page);
		wb = unlocked_inode_to_wb_begin(inode, &locked);

		if (TestClearPageDirty(page))
			account_page_cleaned(page, mapping, wb);

		unlocked_inode_to_wb_end(inode, locked);
		unlock_page_memcg(page);
	} else {
		ClearPageDirty(page);
	}
}
EXPORT_SYMBOL(cancel_dirty_page);

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
	int ret = 0;

	BUG_ON(!PageLocked(page));

	if (mapping && mapping_cap_account_dirty(mapping)) {
		struct inode *inode = mapping->host;
		struct bdi_writeback *wb;
		bool locked;

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
		 * at this point.  We do this by having them hold the
		 * page lock while dirtying the page, and pages are
		 * always locked coming in here, so we get the desired
		 * exclusion.
		 */
		wb = unlocked_inode_to_wb_begin(inode, &locked);
		if (TestClearPageDirty(page)) {
			dec_lruvec_page_state(page, NR_FILE_DIRTY);
			dec_zone_page_state(page, NR_ZONE_WRITE_PENDING);
			dec_wb_stat(wb, WB_RECLAIMABLE);
			ret = 1;
		}
		unlocked_inode_to_wb_end(inode, locked);
		return ret;
	}
	return TestClearPageDirty(page);
}
EXPORT_SYMBOL(clear_page_dirty_for_io);

int test_clear_page_writeback(struct page *page)
{
	struct address_space *mapping = page_mapping(page);
	struct mem_cgroup *memcg;
	struct lruvec *lruvec;
	int ret;

	memcg = lock_page_memcg(page);
	lruvec = mem_cgroup_page_lruvec(page, page_pgdat(page));
	if (mapping && mapping_use_writeback_tags(mapping)) {
		struct inode *inode = mapping->host;
		struct backing_dev_info *bdi = inode_to_bdi(inode);
		unsigned long flags;

		spin_lock_irqsave(&mapping->tree_lock, flags);
		ret = TestClearPageWriteback(page);
		if (ret) {
			radix_tree_tag_clear(&mapping->page_tree,
						page_index(page),
						PAGECACHE_TAG_WRITEBACK);
			if (bdi_cap_account_writeback(bdi)) {
				struct bdi_writeback *wb = inode_to_wb(inode);

				dec_wb_stat(wb, WB_WRITEBACK);
				__wb_writeout_inc(wb);
			}
		}

		if (mapping->host && !mapping_tagged(mapping,
						     PAGECACHE_TAG_WRITEBACK))
			sb_clear_inode_writeback(mapping->host);

		spin_unlock_irqrestore(&mapping->tree_lock, flags);
	} else {
		ret = TestClearPageWriteback(page);
	}
	/*
	 * NOTE: Page might be free now! Writeback doesn't hold a page
	 * reference on its own, it relies on truncation to wait for
	 * the clearing of PG_writeback. The below can only access
	 * page state that is static across allocation cycles.
	 */
	if (ret) {
		dec_lruvec_state(lruvec, NR_WRITEBACK);
		dec_zone_page_state(page, NR_ZONE_WRITE_PENDING);
		inc_node_page_state(page, NR_WRITTEN);
	}
	__unlock_page_memcg(memcg);
	return ret;
}

int __test_set_page_writeback(struct page *page, bool keep_write)
{
	struct address_space *mapping = page_mapping(page);
	int ret;

	lock_page_memcg(page);
	if (mapping && mapping_use_writeback_tags(mapping)) {
		struct inode *inode = mapping->host;
		struct backing_dev_info *bdi = inode_to_bdi(inode);
		unsigned long flags;

		spin_lock_irqsave(&mapping->tree_lock, flags);
		ret = TestSetPageWriteback(page);
		if (!ret) {
			bool on_wblist;

			on_wblist = mapping_tagged(mapping,
						   PAGECACHE_TAG_WRITEBACK);

			radix_tree_tag_set(&mapping->page_tree,
						page_index(page),
						PAGECACHE_TAG_WRITEBACK);
			if (bdi_cap_account_writeback(bdi))
				inc_wb_stat(inode_to_wb(inode), WB_WRITEBACK);

			/*
			 * We can come through here when swapping anonymous
			 * pages, so we don't necessarily have an inode to track
			 * for sync.
			 */
			if (mapping->host && !on_wblist)
				sb_mark_inode_writeback(mapping->host);
		}
		if (!PageDirty(page))
			radix_tree_tag_clear(&mapping->page_tree,
						page_index(page),
						PAGECACHE_TAG_DIRTY);
		if (!keep_write)
			radix_tree_tag_clear(&mapping->page_tree,
						page_index(page),
						PAGECACHE_TAG_TOWRITE);
		spin_unlock_irqrestore(&mapping->tree_lock, flags);
	} else {
		ret = TestSetPageWriteback(page);
	}
	if (!ret) {
		inc_lruvec_page_state(page, NR_WRITEBACK);
		inc_zone_page_state(page, NR_ZONE_WRITE_PENDING);
	}
	unlock_page_memcg(page);
	return ret;

}
EXPORT_SYMBOL(__test_set_page_writeback);

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
	if (bdi_cap_stable_pages_required(inode_to_bdi(page->mapping->host)))
		wait_on_page_writeback(page);
}
EXPORT_SYMBOL_GPL(wait_for_stable_page);
