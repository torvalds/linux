/*
 * mm/page-writeback.c.
 *
 * Copyright (C) 2002, Linus Torvalds.
 *
 * Contains functions related to writing back dirty pages at the
 * address_space level.
 *
 * 10Apr2002	akpm@zip.com.au
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
#include <linux/blkdev.h>
#include <linux/mpage.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/smp.h>
#include <linux/sysctl.h>
#include <linux/cpu.h>
#include <linux/syscalls.h>

/*
 * The maximum number of pages to writeout in a single bdflush/kupdate
 * operation.  We do this so we don't hold I_LOCK against an inode for
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

static long total_pages;	/* The total number of pages in the machine. */
static int dirty_exceeded __cacheline_aligned_in_smp;	/* Dirty mem may be over limit */

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
 * The generator of dirty data starts writeback at this percentage
 */
int vm_dirty_ratio = 40;

/*
 * The interval between `kupdate'-style writebacks, in centiseconds
 * (hundredths of a second)
 */
int dirty_writeback_interval = 5 * HZ;

/*
 * The longest number of centiseconds for which data is allowed to remain dirty
 */
int dirty_expire_interval = 30 * HZ;

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

struct writeback_state
{
	unsigned long nr_dirty;
	unsigned long nr_unstable;
	unsigned long nr_mapped;
	unsigned long nr_writeback;
};

static void get_writeback_state(struct writeback_state *wbs)
{
	wbs->nr_dirty = read_page_state(nr_dirty);
	wbs->nr_unstable = read_page_state(nr_unstable);
	wbs->nr_mapped = read_page_state(nr_mapped);
	wbs->nr_writeback = read_page_state(nr_writeback);
}

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
static void
get_dirty_limits(struct writeback_state *wbs, long *pbackground, long *pdirty,
		struct address_space *mapping)
{
	int background_ratio;		/* Percentages */
	int dirty_ratio;
	int unmapped_ratio;
	long background;
	long dirty;
	unsigned long available_memory = total_pages;
	struct task_struct *tsk;

	get_writeback_state(wbs);

#ifdef CONFIG_HIGHMEM
	/*
	 * If this mapping can only allocate from low memory,
	 * we exclude high memory from our count.
	 */
	if (mapping && !(mapping_gfp_mask(mapping) & __GFP_HIGHMEM))
		available_memory -= totalhigh_pages;
#endif


	unmapped_ratio = 100 - (wbs->nr_mapped * 100) / total_pages;

	dirty_ratio = vm_dirty_ratio;
	if (dirty_ratio > unmapped_ratio / 2)
		dirty_ratio = unmapped_ratio / 2;

	if (dirty_ratio < 5)
		dirty_ratio = 5;

	background_ratio = dirty_background_ratio;
	if (background_ratio >= dirty_ratio)
		background_ratio = dirty_ratio / 2;

	background = (background_ratio * available_memory) / 100;
	dirty = (dirty_ratio * available_memory) / 100;
	tsk = current;
	if (tsk->flags & PF_LESS_THROTTLE || rt_task(tsk)) {
		background += background / 4;
		dirty += dirty / 4;
	}
	*pbackground = background;
	*pdirty = dirty;
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
	struct writeback_state wbs;
	long nr_reclaimable;
	long background_thresh;
	long dirty_thresh;
	unsigned long pages_written = 0;
	unsigned long write_chunk = sync_writeback_pages();

	struct backing_dev_info *bdi = mapping->backing_dev_info;

	for (;;) {
		struct writeback_control wbc = {
			.bdi		= bdi,
			.sync_mode	= WB_SYNC_NONE,
			.older_than_this = NULL,
			.nr_to_write	= write_chunk,
		};

		get_dirty_limits(&wbs, &background_thresh,
					&dirty_thresh, mapping);
		nr_reclaimable = wbs.nr_dirty + wbs.nr_unstable;
		if (nr_reclaimable + wbs.nr_writeback <= dirty_thresh)
			break;

		if (!dirty_exceeded)
			dirty_exceeded = 1;

		/* Note: nr_reclaimable denotes nr_dirty + nr_unstable.
		 * Unstable writes are a feature of certain networked
		 * filesystems (i.e. NFS) in which data may have been
		 * written to the server's write cache, but has not yet
		 * been flushed to permanent storage.
		 */
		if (nr_reclaimable) {
			writeback_inodes(&wbc);
			get_dirty_limits(&wbs, &background_thresh,
					&dirty_thresh, mapping);
			nr_reclaimable = wbs.nr_dirty + wbs.nr_unstable;
			if (nr_reclaimable + wbs.nr_writeback <= dirty_thresh)
				break;
			pages_written += write_chunk - wbc.nr_to_write;
			if (pages_written >= write_chunk)
				break;		/* We've done our duty */
		}
		blk_congestion_wait(WRITE, HZ/10);
	}

	if (nr_reclaimable + wbs.nr_writeback <= dirty_thresh && dirty_exceeded)
		dirty_exceeded = 0;

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
	     (!laptop_mode && (nr_reclaimable > background_thresh)))
		pdflush_operation(background_writeout, 0);
}

/**
 * balance_dirty_pages_ratelimited_nr - balance dirty memory state
 * @mapping: address_space which was dirtied
 * @nr_pages: number of pages which the caller has just dirtied
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
	if (dirty_exceeded)
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

void throttle_vm_writeout(void)
{
	struct writeback_state wbs;
	long background_thresh;
	long dirty_thresh;

        for ( ; ; ) {
		get_dirty_limits(&wbs, &background_thresh, &dirty_thresh, NULL);

                /*
                 * Boost the allowable dirty threshold a bit for page
                 * allocators so they don't get DoS'ed by heavy writers
                 */
                dirty_thresh += dirty_thresh / 10;      /* wheeee... */

                if (wbs.nr_unstable + wbs.nr_writeback <= dirty_thresh)
                        break;
                blk_congestion_wait(WRITE, HZ/10);
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
	};

	for ( ; ; ) {
		struct writeback_state wbs;
		long background_thresh;
		long dirty_thresh;

		get_dirty_limits(&wbs, &background_thresh, &dirty_thresh, NULL);
		if (wbs.nr_dirty + wbs.nr_unstable < background_thresh
				&& min_pages <= 0)
			break;
		wbc.encountered_congestion = 0;
		wbc.nr_to_write = MAX_WRITEBACK_PAGES;
		wbc.pages_skipped = 0;
		writeback_inodes(&wbc);
		min_pages -= MAX_WRITEBACK_PAGES - wbc.nr_to_write;
		if (wbc.nr_to_write > 0 || wbc.pages_skipped > 0) {
			/* Wrote less than expected */
			blk_congestion_wait(WRITE, HZ/10);
			if (!wbc.encountered_congestion)
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
	if (nr_pages == 0) {
		struct writeback_state wbs;

		get_writeback_state(&wbs);
		nr_pages = wbs.nr_dirty + wbs.nr_unstable;
	}
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
	struct writeback_state wbs;
	struct writeback_control wbc = {
		.bdi		= NULL,
		.sync_mode	= WB_SYNC_NONE,
		.older_than_this = &oldest_jif,
		.nr_to_write	= 0,
		.nonblocking	= 1,
		.for_kupdate	= 1,
	};

	sync_supers();

	get_writeback_state(&wbs);
	oldest_jif = jiffies - dirty_expire_interval;
	start_jif = jiffies;
	next_jif = start_jif + dirty_writeback_interval;
	nr_to_write = wbs.nr_dirty + wbs.nr_unstable +
			(inodes_stat.nr_inodes - inodes_stat.nr_unused);
	while (nr_to_write > 0) {
		wbc.encountered_congestion = 0;
		wbc.nr_to_write = MAX_WRITEBACK_PAGES;
		writeback_inodes(&wbc);
		if (wbc.nr_to_write > 0) {
			if (wbc.encountered_congestion)
				blk_congestion_wait(WRITE, HZ/10);
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
	proc_dointvec_userhz_jiffies(table, write, file, buffer, length, ppos);
	if (dirty_writeback_interval) {
		mod_timer(&wb_timer,
			jiffies + dirty_writeback_interval);
		} else {
		del_timer(&wb_timer);
	}
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

static void set_ratelimit(void)
{
	ratelimit_pages = total_pages / (num_online_cpus() * 32);
	if (ratelimit_pages < 16)
		ratelimit_pages = 16;
	if (ratelimit_pages * PAGE_CACHE_SIZE > 4096 * 1024)
		ratelimit_pages = (4096 * 1024) / PAGE_CACHE_SIZE;
}

static int
ratelimit_handler(struct notifier_block *self, unsigned long u, void *v)
{
	set_ratelimit();
	return 0;
}

static struct notifier_block ratelimit_nb = {
	.notifier_call	= ratelimit_handler,
	.next		= NULL,
};

/*
 * If the machine has a large highmem:lowmem ratio then scale back the default
 * dirty memory thresholds: allowing too much dirty highmem pins an excessive
 * number of buffer_heads.
 */
void __init page_writeback_init(void)
{
	long buffer_pages = nr_free_buffer_pages();
	long correction;

	total_pages = nr_free_pagecache_pages();

	correction = (100 * 4 * buffer_pages) / total_pages;

	if (correction < 100) {
		dirty_background_ratio *= correction;
		dirty_background_ratio /= 100;
		vm_dirty_ratio *= correction;
		vm_dirty_ratio /= 100;

		if (dirty_background_ratio <= 0)
			dirty_background_ratio = 1;
		if (vm_dirty_ratio <= 0)
			vm_dirty_ratio = 1;
	}
	mod_timer(&wb_timer, jiffies + dirty_writeback_interval);
	set_ratelimit();
	register_cpu_notifier(&ratelimit_nb);
}

int do_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	int ret;

	if (wbc->nr_to_write <= 0)
		return 0;
	wbc->for_writepages = 1;
	if (mapping->a_ops->writepages)
		ret =  mapping->a_ops->writepages(mapping, wbc);
	else
		ret = generic_writepages(mapping, wbc);
	wbc->for_writepages = 0;
	return ret;
}

/**
 * write_one_page - write out a single page and optionally wait on I/O
 *
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
 * mapping by re-checking page_mapping() insode tree_lock.
 */
int __set_page_dirty_nobuffers(struct page *page)
{
	if (!TestSetPageDirty(page)) {
		struct address_space *mapping = page_mapping(page);
		struct address_space *mapping2;

		if (mapping) {
			write_lock_irq(&mapping->tree_lock);
			mapping2 = page_mapping(page);
			if (mapping2) { /* Race with truncate? */
				BUG_ON(mapping2 != mapping);
				if (mapping_cap_account_dirty(mapping))
					inc_page_state(nr_dirty);
				radix_tree_tag_set(&mapping->page_tree,
					page_index(page), PAGECACHE_TAG_DIRTY);
			}
			write_unlock_irq(&mapping->tree_lock);
			if (mapping->host) {
				/* !PageAnon && !swapper_space */
				__mark_inode_dirty(mapping->host,
							I_DIRTY_PAGES);
			}
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
int fastcall set_page_dirty(struct page *page)
{
	struct address_space *mapping = page_mapping(page);

	if (likely(mapping)) {
		int (*spd)(struct page *) = mapping->a_ops->set_page_dirty;
		if (spd)
			return (*spd)(page);
		return __set_page_dirty_buffers(page);
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
 */
int test_clear_page_dirty(struct page *page)
{
	struct address_space *mapping = page_mapping(page);
	unsigned long flags;

	if (mapping) {
		write_lock_irqsave(&mapping->tree_lock, flags);
		if (TestClearPageDirty(page)) {
			radix_tree_tag_clear(&mapping->page_tree,
						page_index(page),
						PAGECACHE_TAG_DIRTY);
			write_unlock_irqrestore(&mapping->tree_lock, flags);
			if (mapping_cap_account_dirty(mapping))
				dec_page_state(nr_dirty);
			return 1;
		}
		write_unlock_irqrestore(&mapping->tree_lock, flags);
		return 0;
	}
	return TestClearPageDirty(page);
}
EXPORT_SYMBOL(test_clear_page_dirty);

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

	if (mapping) {
		if (TestClearPageDirty(page)) {
			if (mapping_cap_account_dirty(mapping))
				dec_page_state(nr_dirty);
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
		unsigned long flags;

		write_lock_irqsave(&mapping->tree_lock, flags);
		ret = TestClearPageWriteback(page);
		if (ret)
			radix_tree_tag_clear(&mapping->page_tree,
						page_index(page),
						PAGECACHE_TAG_WRITEBACK);
		write_unlock_irqrestore(&mapping->tree_lock, flags);
	} else {
		ret = TestClearPageWriteback(page);
	}
	return ret;
}

int test_set_page_writeback(struct page *page)
{
	struct address_space *mapping = page_mapping(page);
	int ret;

	if (mapping) {
		unsigned long flags;

		write_lock_irqsave(&mapping->tree_lock, flags);
		ret = TestSetPageWriteback(page);
		if (!ret)
			radix_tree_tag_set(&mapping->page_tree,
						page_index(page),
						PAGECACHE_TAG_WRITEBACK);
		if (!PageDirty(page))
			radix_tree_tag_clear(&mapping->page_tree,
						page_index(page),
						PAGECACHE_TAG_DIRTY);
		write_unlock_irqrestore(&mapping->tree_lock, flags);
	} else {
		ret = TestSetPageWriteback(page);
	}
	return ret;

}
EXPORT_SYMBOL(test_set_page_writeback);

/*
 * Return true if any of the pages in the mapping are marged with the
 * passed tag.
 */
int mapping_tagged(struct address_space *mapping, int tag)
{
	unsigned long flags;
	int ret;

	read_lock_irqsave(&mapping->tree_lock, flags);
	ret = radix_tree_tagged(&mapping->page_tree, tag);
	read_unlock_irqrestore(&mapping->tree_lock, flags);
	return ret;
}
EXPORT_SYMBOL(mapping_tagged);
