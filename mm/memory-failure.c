/*
 * Copyright (C) 2008, 2009 Intel Corporation
 * Authors: Andi Kleen, Fengguang Wu
 *
 * This software may be redistributed and/or modified under the terms of
 * the GNU General Public License ("GPL") version 2 only as published by the
 * Free Software Foundation.
 *
 * High level machine check handler. Handles pages reported by the
 * hardware as being corrupted usually due to a multi-bit ECC memory or cache
 * failure.
 * 
 * In addition there is a "soft offline" entry point that allows stop using
 * not-yet-corrupted-by-suspicious pages without killing anything.
 *
 * Handles page cache pages in various states.	The tricky part
 * here is that we can access any page asynchronously in respect to 
 * other VM users, because memory failures could happen anytime and 
 * anywhere. This could violate some of their assumptions. This is why 
 * this code has to be extremely careful. Generally it tries to use 
 * normal locking rules, as in get the standard locks, even if that means 
 * the error handling takes potentially a long time.
 * 
 * There are several operations here with exponential complexity because
 * of unsuitable VM data structures. For example the operation to map back 
 * from RMAP chains to processes has to walk the complete process list and 
 * has non linear complexity with the number. But since memory corruptions
 * are rare we hope to get away with this. This avoids impacting the core 
 * VM.
 */

/*
 * Notebook:
 * - hugetlb needs more code
 * - kcore/oldmem/vmcore/mem/kmem check for hwpoison pages
 * - pass bad pages to kdump next kernel
 */
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/page-flags.h>
#include <linux/kernel-page-flags.h>
#include <linux/sched.h>
#include <linux/ksm.h>
#include <linux/rmap.h>
#include <linux/export.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/backing-dev.h>
#include <linux/migrate.h>
#include <linux/page-isolation.h>
#include <linux/suspend.h>
#include <linux/slab.h>
#include <linux/swapops.h>
#include <linux/hugetlb.h>
#include <linux/memory_hotplug.h>
#include <linux/mm_inline.h>
#include <linux/kfifo.h>
#include "internal.h"

int sysctl_memory_failure_early_kill __read_mostly = 0;

int sysctl_memory_failure_recovery __read_mostly = 1;

atomic_long_t num_poisoned_pages __read_mostly = ATOMIC_LONG_INIT(0);

#if defined(CONFIG_HWPOISON_INJECT) || defined(CONFIG_HWPOISON_INJECT_MODULE)

u32 hwpoison_filter_enable = 0;
u32 hwpoison_filter_dev_major = ~0U;
u32 hwpoison_filter_dev_minor = ~0U;
u64 hwpoison_filter_flags_mask;
u64 hwpoison_filter_flags_value;
EXPORT_SYMBOL_GPL(hwpoison_filter_enable);
EXPORT_SYMBOL_GPL(hwpoison_filter_dev_major);
EXPORT_SYMBOL_GPL(hwpoison_filter_dev_minor);
EXPORT_SYMBOL_GPL(hwpoison_filter_flags_mask);
EXPORT_SYMBOL_GPL(hwpoison_filter_flags_value);

static int hwpoison_filter_dev(struct page *p)
{
	struct address_space *mapping;
	dev_t dev;

	if (hwpoison_filter_dev_major == ~0U &&
	    hwpoison_filter_dev_minor == ~0U)
		return 0;

	/*
	 * page_mapping() does not accept slab pages.
	 */
	if (PageSlab(p))
		return -EINVAL;

	mapping = page_mapping(p);
	if (mapping == NULL || mapping->host == NULL)
		return -EINVAL;

	dev = mapping->host->i_sb->s_dev;
	if (hwpoison_filter_dev_major != ~0U &&
	    hwpoison_filter_dev_major != MAJOR(dev))
		return -EINVAL;
	if (hwpoison_filter_dev_minor != ~0U &&
	    hwpoison_filter_dev_minor != MINOR(dev))
		return -EINVAL;

	return 0;
}

static int hwpoison_filter_flags(struct page *p)
{
	if (!hwpoison_filter_flags_mask)
		return 0;

	if ((stable_page_flags(p) & hwpoison_filter_flags_mask) ==
				    hwpoison_filter_flags_value)
		return 0;
	else
		return -EINVAL;
}

/*
 * This allows stress tests to limit test scope to a collection of tasks
 * by putting them under some memcg. This prevents killing unrelated/important
 * processes such as /sbin/init. Note that the target task may share clean
 * pages with init (eg. libc text), which is harmless. If the target task
 * share _dirty_ pages with another task B, the test scheme must make sure B
 * is also included in the memcg. At last, due to race conditions this filter
 * can only guarantee that the page either belongs to the memcg tasks, or is
 * a freed page.
 */
#ifdef	CONFIG_MEMCG_SWAP
u64 hwpoison_filter_memcg;
EXPORT_SYMBOL_GPL(hwpoison_filter_memcg);
static int hwpoison_filter_task(struct page *p)
{
	struct mem_cgroup *mem;
	struct cgroup_subsys_state *css;
	unsigned long ino;

	if (!hwpoison_filter_memcg)
		return 0;

	mem = try_get_mem_cgroup_from_page(p);
	if (!mem)
		return -EINVAL;

	css = mem_cgroup_css(mem);
	/* root_mem_cgroup has NULL dentries */
	if (!css->cgroup->dentry)
		return -EINVAL;

	ino = css->cgroup->dentry->d_inode->i_ino;
	css_put(css);

	if (ino != hwpoison_filter_memcg)
		return -EINVAL;

	return 0;
}
#else
static int hwpoison_filter_task(struct page *p) { return 0; }
#endif

int hwpoison_filter(struct page *p)
{
	if (!hwpoison_filter_enable)
		return 0;

	if (hwpoison_filter_dev(p))
		return -EINVAL;

	if (hwpoison_filter_flags(p))
		return -EINVAL;

	if (hwpoison_filter_task(p))
		return -EINVAL;

	return 0;
}
#else
int hwpoison_filter(struct page *p)
{
	return 0;
}
#endif

EXPORT_SYMBOL_GPL(hwpoison_filter);

/*
 * Send all the processes who have the page mapped a signal.
 * ``action optional'' if they are not immediately affected by the error
 * ``action required'' if error happened in current execution context
 */
static int kill_proc(struct task_struct *t, unsigned long addr, int trapno,
			unsigned long pfn, struct page *page, int flags)
{
	struct siginfo si;
	int ret;

	printk(KERN_ERR
		"MCE %#lx: Killing %s:%d due to hardware memory corruption\n",
		pfn, t->comm, t->pid);
	si.si_signo = SIGBUS;
	si.si_errno = 0;
	si.si_addr = (void *)addr;
#ifdef __ARCH_SI_TRAPNO
	si.si_trapno = trapno;
#endif
	si.si_addr_lsb = compound_order(compound_head(page)) + PAGE_SHIFT;

	if ((flags & MF_ACTION_REQUIRED) && t == current) {
		si.si_code = BUS_MCEERR_AR;
		ret = force_sig_info(SIGBUS, &si, t);
	} else {
		/*
		 * Don't use force here, it's convenient if the signal
		 * can be temporarily blocked.
		 * This could cause a loop when the user sets SIGBUS
		 * to SIG_IGN, but hopefully no one will do that?
		 */
		si.si_code = BUS_MCEERR_AO;
		ret = send_sig_info(SIGBUS, &si, t);  /* synchronous? */
	}
	if (ret < 0)
		printk(KERN_INFO "MCE: Error sending signal to %s:%d: %d\n",
		       t->comm, t->pid, ret);
	return ret;
}

/*
 * When a unknown page type is encountered drain as many buffers as possible
 * in the hope to turn the page into a LRU or free page, which we can handle.
 */
void shake_page(struct page *p, int access)
{
	if (!PageSlab(p)) {
		lru_add_drain_all();
		if (PageLRU(p))
			return;
		drain_all_pages();
		if (PageLRU(p) || is_free_buddy_page(p))
			return;
	}

	/*
	 * Only call shrink_slab here (which would also shrink other caches) if
	 * access is not potentially fatal.
	 */
	if (access) {
		int nr;
		int nid = page_to_nid(p);
		do {
			struct shrink_control shrink = {
				.gfp_mask = GFP_KERNEL,
			};
			node_set(nid, shrink.nodes_to_scan);

			nr = shrink_slab(&shrink, 1000, 1000);
			if (page_count(p) == 1)
				break;
		} while (nr > 10);
	}
}
EXPORT_SYMBOL_GPL(shake_page);

/*
 * Kill all processes that have a poisoned page mapped and then isolate
 * the page.
 *
 * General strategy:
 * Find all processes having the page mapped and kill them.
 * But we keep a page reference around so that the page is not
 * actually freed yet.
 * Then stash the page away
 *
 * There's no convenient way to get back to mapped processes
 * from the VMAs. So do a brute-force search over all
 * running processes.
 *
 * Remember that machine checks are not common (or rather
 * if they are common you have other problems), so this shouldn't
 * be a performance issue.
 *
 * Also there are some races possible while we get from the
 * error detection to actually handle it.
 */

struct to_kill {
	struct list_head nd;
	struct task_struct *tsk;
	unsigned long addr;
	char addr_valid;
};

/*
 * Failure handling: if we can't find or can't kill a process there's
 * not much we can do.	We just print a message and ignore otherwise.
 */

/*
 * Schedule a process for later kill.
 * Uses GFP_ATOMIC allocations to avoid potential recursions in the VM.
 * TBD would GFP_NOIO be enough?
 */
static void add_to_kill(struct task_struct *tsk, struct page *p,
		       struct vm_area_struct *vma,
		       struct list_head *to_kill,
		       struct to_kill **tkc)
{
	struct to_kill *tk;

	if (*tkc) {
		tk = *tkc;
		*tkc = NULL;
	} else {
		tk = kmalloc(sizeof(struct to_kill), GFP_ATOMIC);
		if (!tk) {
			printk(KERN_ERR
		"MCE: Out of memory while machine check handling\n");
			return;
		}
	}
	tk->addr = page_address_in_vma(p, vma);
	tk->addr_valid = 1;

	/*
	 * In theory we don't have to kill when the page was
	 * munmaped. But it could be also a mremap. Since that's
	 * likely very rare kill anyways just out of paranoia, but use
	 * a SIGKILL because the error is not contained anymore.
	 */
	if (tk->addr == -EFAULT) {
		pr_info("MCE: Unable to find user space address %lx in %s\n",
			page_to_pfn(p), tsk->comm);
		tk->addr_valid = 0;
	}
	get_task_struct(tsk);
	tk->tsk = tsk;
	list_add_tail(&tk->nd, to_kill);
}

/*
 * Kill the processes that have been collected earlier.
 *
 * Only do anything when DOIT is set, otherwise just free the list
 * (this is used for clean pages which do not need killing)
 * Also when FAIL is set do a force kill because something went
 * wrong earlier.
 */
static void kill_procs(struct list_head *to_kill, int forcekill, int trapno,
			  int fail, struct page *page, unsigned long pfn,
			  int flags)
{
	struct to_kill *tk, *next;

	list_for_each_entry_safe (tk, next, to_kill, nd) {
		if (forcekill) {
			/*
			 * In case something went wrong with munmapping
			 * make sure the process doesn't catch the
			 * signal and then access the memory. Just kill it.
			 */
			if (fail || tk->addr_valid == 0) {
				printk(KERN_ERR
		"MCE %#lx: forcibly killing %s:%d because of failure to unmap corrupted page\n",
					pfn, tk->tsk->comm, tk->tsk->pid);
				force_sig(SIGKILL, tk->tsk);
			}

			/*
			 * In theory the process could have mapped
			 * something else on the address in-between. We could
			 * check for that, but we need to tell the
			 * process anyways.
			 */
			else if (kill_proc(tk->tsk, tk->addr, trapno,
					      pfn, page, flags) < 0)
				printk(KERN_ERR
		"MCE %#lx: Cannot send advisory machine check signal to %s:%d\n",
					pfn, tk->tsk->comm, tk->tsk->pid);
		}
		put_task_struct(tk->tsk);
		kfree(tk);
	}
}

static int task_early_kill(struct task_struct *tsk)
{
	if (!tsk->mm)
		return 0;
	if (tsk->flags & PF_MCE_PROCESS)
		return !!(tsk->flags & PF_MCE_EARLY);
	return sysctl_memory_failure_early_kill;
}

/*
 * Collect processes when the error hit an anonymous page.
 */
static void collect_procs_anon(struct page *page, struct list_head *to_kill,
			      struct to_kill **tkc)
{
	struct vm_area_struct *vma;
	struct task_struct *tsk;
	struct anon_vma *av;
	pgoff_t pgoff;

	av = page_lock_anon_vma_read(page);
	if (av == NULL)	/* Not actually mapped anymore */
		return;

	pgoff = page->index << (PAGE_CACHE_SHIFT - PAGE_SHIFT);
	read_lock(&tasklist_lock);
	for_each_process (tsk) {
		struct anon_vma_chain *vmac;

		if (!task_early_kill(tsk))
			continue;
		anon_vma_interval_tree_foreach(vmac, &av->rb_root,
					       pgoff, pgoff) {
			vma = vmac->vma;
			if (!page_mapped_in_vma(page, vma))
				continue;
			if (vma->vm_mm == tsk->mm)
				add_to_kill(tsk, page, vma, to_kill, tkc);
		}
	}
	read_unlock(&tasklist_lock);
	page_unlock_anon_vma_read(av);
}

/*
 * Collect processes when the error hit a file mapped page.
 */
static void collect_procs_file(struct page *page, struct list_head *to_kill,
			      struct to_kill **tkc)
{
	struct vm_area_struct *vma;
	struct task_struct *tsk;
	struct address_space *mapping = page->mapping;

	mutex_lock(&mapping->i_mmap_mutex);
	read_lock(&tasklist_lock);
	for_each_process(tsk) {
		pgoff_t pgoff = page->index << (PAGE_CACHE_SHIFT - PAGE_SHIFT);

		if (!task_early_kill(tsk))
			continue;

		vma_interval_tree_foreach(vma, &mapping->i_mmap, pgoff,
				      pgoff) {
			/*
			 * Send early kill signal to tasks where a vma covers
			 * the page but the corrupted page is not necessarily
			 * mapped it in its pte.
			 * Assume applications who requested early kill want
			 * to be informed of all such data corruptions.
			 */
			if (vma->vm_mm == tsk->mm)
				add_to_kill(tsk, page, vma, to_kill, tkc);
		}
	}
	read_unlock(&tasklist_lock);
	mutex_unlock(&mapping->i_mmap_mutex);
}

/*
 * Collect the processes who have the corrupted page mapped to kill.
 * This is done in two steps for locking reasons.
 * First preallocate one tokill structure outside the spin locks,
 * so that we can kill at least one process reasonably reliable.
 */
static void collect_procs(struct page *page, struct list_head *tokill)
{
	struct to_kill *tk;

	if (!page->mapping)
		return;

	tk = kmalloc(sizeof(struct to_kill), GFP_NOIO);
	if (!tk)
		return;
	if (PageAnon(page))
		collect_procs_anon(page, tokill, &tk);
	else
		collect_procs_file(page, tokill, &tk);
	kfree(tk);
}

/*
 * Error handlers for various types of pages.
 */

enum outcome {
	IGNORED,	/* Error: cannot be handled */
	FAILED,		/* Error: handling failed */
	DELAYED,	/* Will be handled later */
	RECOVERED,	/* Successfully recovered */
};

static const char *action_name[] = {
	[IGNORED] = "Ignored",
	[FAILED] = "Failed",
	[DELAYED] = "Delayed",
	[RECOVERED] = "Recovered",
};

/*
 * XXX: It is possible that a page is isolated from LRU cache,
 * and then kept in swap cache or failed to remove from page cache.
 * The page count will stop it from being freed by unpoison.
 * Stress tests should be aware of this memory leak problem.
 */
static int delete_from_lru_cache(struct page *p)
{
	if (!isolate_lru_page(p)) {
		/*
		 * Clear sensible page flags, so that the buddy system won't
		 * complain when the page is unpoison-and-freed.
		 */
		ClearPageActive(p);
		ClearPageUnevictable(p);
		/*
		 * drop the page count elevated by isolate_lru_page()
		 */
		page_cache_release(p);
		return 0;
	}
	return -EIO;
}

/*
 * Error hit kernel page.
 * Do nothing, try to be lucky and not touch this instead. For a few cases we
 * could be more sophisticated.
 */
static int me_kernel(struct page *p, unsigned long pfn)
{
	return IGNORED;
}

/*
 * Page in unknown state. Do nothing.
 */
static int me_unknown(struct page *p, unsigned long pfn)
{
	printk(KERN_ERR "MCE %#lx: Unknown page state\n", pfn);
	return FAILED;
}

/*
 * Clean (or cleaned) page cache page.
 */
static int me_pagecache_clean(struct page *p, unsigned long pfn)
{
	int err;
	int ret = FAILED;
	struct address_space *mapping;

	delete_from_lru_cache(p);

	/*
	 * For anonymous pages we're done the only reference left
	 * should be the one m_f() holds.
	 */
	if (PageAnon(p))
		return RECOVERED;

	/*
	 * Now truncate the page in the page cache. This is really
	 * more like a "temporary hole punch"
	 * Don't do this for block devices when someone else
	 * has a reference, because it could be file system metadata
	 * and that's not safe to truncate.
	 */
	mapping = page_mapping(p);
	if (!mapping) {
		/*
		 * Page has been teared down in the meanwhile
		 */
		return FAILED;
	}

	/*
	 * Truncation is a bit tricky. Enable it per file system for now.
	 *
	 * Open: to take i_mutex or not for this? Right now we don't.
	 */
	if (mapping->a_ops->error_remove_page) {
		err = mapping->a_ops->error_remove_page(mapping, p);
		if (err != 0) {
			printk(KERN_INFO "MCE %#lx: Failed to punch page: %d\n",
					pfn, err);
		} else if (page_has_private(p) &&
				!try_to_release_page(p, GFP_NOIO)) {
			pr_info("MCE %#lx: failed to release buffers\n", pfn);
		} else {
			ret = RECOVERED;
		}
	} else {
		/*
		 * If the file system doesn't support it just invalidate
		 * This fails on dirty or anything with private pages
		 */
		if (invalidate_inode_page(p))
			ret = RECOVERED;
		else
			printk(KERN_INFO "MCE %#lx: Failed to invalidate\n",
				pfn);
	}
	return ret;
}

/*
 * Dirty cache page page
 * Issues: when the error hit a hole page the error is not properly
 * propagated.
 */
static int me_pagecache_dirty(struct page *p, unsigned long pfn)
{
	struct address_space *mapping = page_mapping(p);

	SetPageError(p);
	/* TBD: print more information about the file. */
	if (mapping) {
		/*
		 * IO error will be reported by write(), fsync(), etc.
		 * who check the mapping.
		 * This way the application knows that something went
		 * wrong with its dirty file data.
		 *
		 * There's one open issue:
		 *
		 * The EIO will be only reported on the next IO
		 * operation and then cleared through the IO map.
		 * Normally Linux has two mechanisms to pass IO error
		 * first through the AS_EIO flag in the address space
		 * and then through the PageError flag in the page.
		 * Since we drop pages on memory failure handling the
		 * only mechanism open to use is through AS_AIO.
		 *
		 * This has the disadvantage that it gets cleared on
		 * the first operation that returns an error, while
		 * the PageError bit is more sticky and only cleared
		 * when the page is reread or dropped.  If an
		 * application assumes it will always get error on
		 * fsync, but does other operations on the fd before
		 * and the page is dropped between then the error
		 * will not be properly reported.
		 *
		 * This can already happen even without hwpoisoned
		 * pages: first on metadata IO errors (which only
		 * report through AS_EIO) or when the page is dropped
		 * at the wrong time.
		 *
		 * So right now we assume that the application DTRT on
		 * the first EIO, but we're not worse than other parts
		 * of the kernel.
		 */
		mapping_set_error(mapping, EIO);
	}

	return me_pagecache_clean(p, pfn);
}

/*
 * Clean and dirty swap cache.
 *
 * Dirty swap cache page is tricky to handle. The page could live both in page
 * cache and swap cache(ie. page is freshly swapped in). So it could be
 * referenced concurrently by 2 types of PTEs:
 * normal PTEs and swap PTEs. We try to handle them consistently by calling
 * try_to_unmap(TTU_IGNORE_HWPOISON) to convert the normal PTEs to swap PTEs,
 * and then
 *      - clear dirty bit to prevent IO
 *      - remove from LRU
 *      - but keep in the swap cache, so that when we return to it on
 *        a later page fault, we know the application is accessing
 *        corrupted data and shall be killed (we installed simple
 *        interception code in do_swap_page to catch it).
 *
 * Clean swap cache pages can be directly isolated. A later page fault will
 * bring in the known good data from disk.
 */
static int me_swapcache_dirty(struct page *p, unsigned long pfn)
{
	ClearPageDirty(p);
	/* Trigger EIO in shmem: */
	ClearPageUptodate(p);

	if (!delete_from_lru_cache(p))
		return DELAYED;
	else
		return FAILED;
}

static int me_swapcache_clean(struct page *p, unsigned long pfn)
{
	delete_from_swap_cache(p);

	if (!delete_from_lru_cache(p))
		return RECOVERED;
	else
		return FAILED;
}

/*
 * Huge pages. Needs work.
 * Issues:
 * - Error on hugepage is contained in hugepage unit (not in raw page unit.)
 *   To narrow down kill region to one page, we need to break up pmd.
 */
static int me_huge_page(struct page *p, unsigned long pfn)
{
	int res = 0;
	struct page *hpage = compound_head(p);
	/*
	 * We can safely recover from error on free or reserved (i.e.
	 * not in-use) hugepage by dequeuing it from freelist.
	 * To check whether a hugepage is in-use or not, we can't use
	 * page->lru because it can be used in other hugepage operations,
	 * such as __unmap_hugepage_range() and gather_surplus_pages().
	 * So instead we use page_mapping() and PageAnon().
	 * We assume that this function is called with page lock held,
	 * so there is no race between isolation and mapping/unmapping.
	 */
	if (!(page_mapping(hpage) || PageAnon(hpage))) {
		res = dequeue_hwpoisoned_huge_page(hpage);
		if (!res)
			return RECOVERED;
	}
	return DELAYED;
}

/*
 * Various page states we can handle.
 *
 * A page state is defined by its current page->flags bits.
 * The table matches them in order and calls the right handler.
 *
 * This is quite tricky because we can access page at any time
 * in its live cycle, so all accesses have to be extremely careful.
 *
 * This is not complete. More states could be added.
 * For any missing state don't attempt recovery.
 */

#define dirty		(1UL << PG_dirty)
#define sc		(1UL << PG_swapcache)
#define unevict		(1UL << PG_unevictable)
#define mlock		(1UL << PG_mlocked)
#define writeback	(1UL << PG_writeback)
#define lru		(1UL << PG_lru)
#define swapbacked	(1UL << PG_swapbacked)
#define head		(1UL << PG_head)
#define tail		(1UL << PG_tail)
#define compound	(1UL << PG_compound)
#define slab		(1UL << PG_slab)
#define reserved	(1UL << PG_reserved)

static struct page_state {
	unsigned long mask;
	unsigned long res;
	char *msg;
	int (*action)(struct page *p, unsigned long pfn);
} error_states[] = {
	{ reserved,	reserved,	"reserved kernel",	me_kernel },
	/*
	 * free pages are specially detected outside this table:
	 * PG_buddy pages only make a small fraction of all free pages.
	 */

	/*
	 * Could in theory check if slab page is free or if we can drop
	 * currently unused objects without touching them. But just
	 * treat it as standard kernel for now.
	 */
	{ slab,		slab,		"kernel slab",	me_kernel },

#ifdef CONFIG_PAGEFLAGS_EXTENDED
	{ head,		head,		"huge",		me_huge_page },
	{ tail,		tail,		"huge",		me_huge_page },
#else
	{ compound,	compound,	"huge",		me_huge_page },
#endif

	{ sc|dirty,	sc|dirty,	"dirty swapcache",	me_swapcache_dirty },
	{ sc|dirty,	sc,		"clean swapcache",	me_swapcache_clean },

	{ mlock|dirty,	mlock|dirty,	"dirty mlocked LRU",	me_pagecache_dirty },
	{ mlock|dirty,	mlock,		"clean mlocked LRU",	me_pagecache_clean },

	{ unevict|dirty, unevict|dirty,	"dirty unevictable LRU", me_pagecache_dirty },
	{ unevict|dirty, unevict,	"clean unevictable LRU", me_pagecache_clean },

	{ lru|dirty,	lru|dirty,	"dirty LRU",	me_pagecache_dirty },
	{ lru|dirty,	lru,		"clean LRU",	me_pagecache_clean },

	/*
	 * Catchall entry: must be at end.
	 */
	{ 0,		0,		"unknown page state",	me_unknown },
};

#undef dirty
#undef sc
#undef unevict
#undef mlock
#undef writeback
#undef lru
#undef swapbacked
#undef head
#undef tail
#undef compound
#undef slab
#undef reserved

/*
 * "Dirty/Clean" indication is not 100% accurate due to the possibility of
 * setting PG_dirty outside page lock. See also comment above set_page_dirty().
 */
static void action_result(unsigned long pfn, char *msg, int result)
{
	pr_err("MCE %#lx: %s page recovery: %s\n",
		pfn, msg, action_name[result]);
}

static int page_action(struct page_state *ps, struct page *p,
			unsigned long pfn)
{
	int result;
	int count;

	result = ps->action(p, pfn);
	action_result(pfn, ps->msg, result);

	count = page_count(p) - 1;
	if (ps->action == me_swapcache_dirty && result == DELAYED)
		count--;
	if (count != 0) {
		printk(KERN_ERR
		       "MCE %#lx: %s page still referenced by %d users\n",
		       pfn, ps->msg, count);
		result = FAILED;
	}

	/* Could do more checks here if page looks ok */
	/*
	 * Could adjust zone counters here to correct for the missing page.
	 */

	return (result == RECOVERED || result == DELAYED) ? 0 : -EBUSY;
}

/*
 * Do all that is necessary to remove user space mappings. Unmap
 * the pages and send SIGBUS to the processes if the data was dirty.
 */
static int hwpoison_user_mappings(struct page *p, unsigned long pfn,
				  int trapno, int flags)
{
	enum ttu_flags ttu = TTU_UNMAP | TTU_IGNORE_MLOCK | TTU_IGNORE_ACCESS;
	struct address_space *mapping;
	LIST_HEAD(tokill);
	int ret;
	int kill = 1, forcekill;
	struct page *hpage = compound_head(p);
	struct page *ppage;

	if (PageReserved(p) || PageSlab(p))
		return SWAP_SUCCESS;

	/*
	 * This check implies we don't kill processes if their pages
	 * are in the swap cache early. Those are always late kills.
	 */
	if (!page_mapped(hpage))
		return SWAP_SUCCESS;

	if (PageKsm(p))
		return SWAP_FAIL;

	if (PageSwapCache(p)) {
		printk(KERN_ERR
		       "MCE %#lx: keeping poisoned page in swap cache\n", pfn);
		ttu |= TTU_IGNORE_HWPOISON;
	}

	/*
	 * Propagate the dirty bit from PTEs to struct page first, because we
	 * need this to decide if we should kill or just drop the page.
	 * XXX: the dirty test could be racy: set_page_dirty() may not always
	 * be called inside page lock (it's recommended but not enforced).
	 */
	mapping = page_mapping(hpage);
	if (!(flags & MF_MUST_KILL) && !PageDirty(hpage) && mapping &&
	    mapping_cap_writeback_dirty(mapping)) {
		if (page_mkclean(hpage)) {
			SetPageDirty(hpage);
		} else {
			kill = 0;
			ttu |= TTU_IGNORE_HWPOISON;
			printk(KERN_INFO
	"MCE %#lx: corrupted page was clean: dropped without side effects\n",
				pfn);
		}
	}

	/*
	 * ppage: poisoned page
	 *   if p is regular page(4k page)
	 *        ppage == real poisoned page;
	 *   else p is hugetlb or THP, ppage == head page.
	 */
	ppage = hpage;

	if (PageTransHuge(hpage)) {
		/*
		 * Verify that this isn't a hugetlbfs head page, the check for
		 * PageAnon is just for avoid tripping a split_huge_page
		 * internal debug check, as split_huge_page refuses to deal with
		 * anything that isn't an anon page. PageAnon can't go away fro
		 * under us because we hold a refcount on the hpage, without a
		 * refcount on the hpage. split_huge_page can't be safely called
		 * in the first place, having a refcount on the tail isn't
		 * enough * to be safe.
		 */
		if (!PageHuge(hpage) && PageAnon(hpage)) {
			if (unlikely(split_huge_page(hpage))) {
				/*
				 * FIXME: if splitting THP is failed, it is
				 * better to stop the following operation rather
				 * than causing panic by unmapping. System might
				 * survive if the page is freed later.
				 */
				printk(KERN_INFO
					"MCE %#lx: failed to split THP\n", pfn);

				BUG_ON(!PageHWPoison(p));
				return SWAP_FAIL;
			}
			/* THP is split, so ppage should be the real poisoned page. */
			ppage = p;
		}
	}

	/*
	 * First collect all the processes that have the page
	 * mapped in dirty form.  This has to be done before try_to_unmap,
	 * because ttu takes the rmap data structures down.
	 *
	 * Error handling: We ignore errors here because
	 * there's nothing that can be done.
	 */
	if (kill)
		collect_procs(ppage, &tokill);

	if (hpage != ppage)
		lock_page(ppage);

	ret = try_to_unmap(ppage, ttu);
	if (ret != SWAP_SUCCESS)
		printk(KERN_ERR "MCE %#lx: failed to unmap page (mapcount=%d)\n",
				pfn, page_mapcount(ppage));

	if (hpage != ppage)
		unlock_page(ppage);

	/*
	 * Now that the dirty bit has been propagated to the
	 * struct page and all unmaps done we can decide if
	 * killing is needed or not.  Only kill when the page
	 * was dirty or the process is not restartable,
	 * otherwise the tokill list is merely
	 * freed.  When there was a problem unmapping earlier
	 * use a more force-full uncatchable kill to prevent
	 * any accesses to the poisoned memory.
	 */
	forcekill = PageDirty(ppage) || (flags & MF_MUST_KILL);
	kill_procs(&tokill, forcekill, trapno,
		      ret != SWAP_SUCCESS, p, pfn, flags);

	return ret;
}

static void set_page_hwpoison_huge_page(struct page *hpage)
{
	int i;
	int nr_pages = 1 << compound_order(hpage);
	for (i = 0; i < nr_pages; i++)
		SetPageHWPoison(hpage + i);
}

static void clear_page_hwpoison_huge_page(struct page *hpage)
{
	int i;
	int nr_pages = 1 << compound_order(hpage);
	for (i = 0; i < nr_pages; i++)
		ClearPageHWPoison(hpage + i);
}

/**
 * memory_failure - Handle memory failure of a page.
 * @pfn: Page Number of the corrupted page
 * @trapno: Trap number reported in the signal to user space.
 * @flags: fine tune action taken
 *
 * This function is called by the low level machine check code
 * of an architecture when it detects hardware memory corruption
 * of a page. It tries its best to recover, which includes
 * dropping pages, killing processes etc.
 *
 * The function is primarily of use for corruptions that
 * happen outside the current execution context (e.g. when
 * detected by a background scrubber)
 *
 * Must run in process context (e.g. a work queue) with interrupts
 * enabled and no spinlocks hold.
 */
int memory_failure(unsigned long pfn, int trapno, int flags)
{
	struct page_state *ps;
	struct page *p;
	struct page *hpage;
	int res;
	unsigned int nr_pages;
	unsigned long page_flags;

	if (!sysctl_memory_failure_recovery)
		panic("Memory failure from trap %d on page %lx", trapno, pfn);

	if (!pfn_valid(pfn)) {
		printk(KERN_ERR
		       "MCE %#lx: memory outside kernel control\n",
		       pfn);
		return -ENXIO;
	}

	p = pfn_to_page(pfn);
	hpage = compound_head(p);
	if (TestSetPageHWPoison(p)) {
		printk(KERN_ERR "MCE %#lx: already hardware poisoned\n", pfn);
		return 0;
	}

	/*
	 * Currently errors on hugetlbfs pages are measured in hugepage units,
	 * so nr_pages should be 1 << compound_order.  OTOH when errors are on
	 * transparent hugepages, they are supposed to be split and error
	 * measurement is done in normal page units.  So nr_pages should be one
	 * in this case.
	 */
	if (PageHuge(p))
		nr_pages = 1 << compound_order(hpage);
	else /* normal page or thp */
		nr_pages = 1;
	atomic_long_add(nr_pages, &num_poisoned_pages);

	/*
	 * We need/can do nothing about count=0 pages.
	 * 1) it's a free page, and therefore in safe hand:
	 *    prep_new_page() will be the gate keeper.
	 * 2) it's a free hugepage, which is also safe:
	 *    an affected hugepage will be dequeued from hugepage freelist,
	 *    so there's no concern about reusing it ever after.
	 * 3) it's part of a non-compound high order page.
	 *    Implies some kernel user: cannot stop them from
	 *    R/W the page; let's pray that the page has been
	 *    used and will be freed some time later.
	 * In fact it's dangerous to directly bump up page count from 0,
	 * that may make page_freeze_refs()/page_unfreeze_refs() mismatch.
	 */
	if (!(flags & MF_COUNT_INCREASED) &&
		!get_page_unless_zero(hpage)) {
		if (is_free_buddy_page(p)) {
			action_result(pfn, "free buddy", DELAYED);
			return 0;
		} else if (PageHuge(hpage)) {
			/*
			 * Check "just unpoisoned", "filter hit", and
			 * "race with other subpage."
			 */
			lock_page(hpage);
			if (!PageHWPoison(hpage)
			    || (hwpoison_filter(p) && TestClearPageHWPoison(p))
			    || (p != hpage && TestSetPageHWPoison(hpage))) {
				atomic_long_sub(nr_pages, &num_poisoned_pages);
				return 0;
			}
			set_page_hwpoison_huge_page(hpage);
			res = dequeue_hwpoisoned_huge_page(hpage);
			action_result(pfn, "free huge",
				      res ? IGNORED : DELAYED);
			unlock_page(hpage);
			return res;
		} else {
			action_result(pfn, "high order kernel", IGNORED);
			return -EBUSY;
		}
	}

	/*
	 * We ignore non-LRU pages for good reasons.
	 * - PG_locked is only well defined for LRU pages and a few others
	 * - to avoid races with __set_page_locked()
	 * - to avoid races with __SetPageSlab*() (and more non-atomic ops)
	 * The check (unnecessarily) ignores LRU pages being isolated and
	 * walked by the page reclaim code, however that's not a big loss.
	 */
	if (!PageHuge(p) && !PageTransTail(p)) {
		if (!PageLRU(p))
			shake_page(p, 0);
		if (!PageLRU(p)) {
			/*
			 * shake_page could have turned it free.
			 */
			if (is_free_buddy_page(p)) {
				if (flags & MF_COUNT_INCREASED)
					action_result(pfn, "free buddy", DELAYED);
				else
					action_result(pfn, "free buddy, 2nd try", DELAYED);
				return 0;
			}
			action_result(pfn, "non LRU", IGNORED);
			put_page(p);
			return -EBUSY;
		}
	}

	/*
	 * Lock the page and wait for writeback to finish.
	 * It's very difficult to mess with pages currently under IO
	 * and in many cases impossible, so we just avoid it here.
	 */
	lock_page(hpage);

	/*
	 * We use page flags to determine what action should be taken, but
	 * the flags can be modified by the error containment action.  One
	 * example is an mlocked page, where PG_mlocked is cleared by
	 * page_remove_rmap() in try_to_unmap_one(). So to determine page status
	 * correctly, we save a copy of the page flags at this time.
	 */
	page_flags = p->flags;

	/*
	 * unpoison always clear PG_hwpoison inside page lock
	 */
	if (!PageHWPoison(p)) {
		printk(KERN_ERR "MCE %#lx: just unpoisoned\n", pfn);
		res = 0;
		goto out;
	}
	if (hwpoison_filter(p)) {
		if (TestClearPageHWPoison(p))
			atomic_long_sub(nr_pages, &num_poisoned_pages);
		unlock_page(hpage);
		put_page(hpage);
		return 0;
	}

	/*
	 * For error on the tail page, we should set PG_hwpoison
	 * on the head page to show that the hugepage is hwpoisoned
	 */
	if (PageHuge(p) && PageTail(p) && TestSetPageHWPoison(hpage)) {
		action_result(pfn, "hugepage already hardware poisoned",
				IGNORED);
		unlock_page(hpage);
		put_page(hpage);
		return 0;
	}
	/*
	 * Set PG_hwpoison on all pages in an error hugepage,
	 * because containment is done in hugepage unit for now.
	 * Since we have done TestSetPageHWPoison() for the head page with
	 * page lock held, we can safely set PG_hwpoison bits on tail pages.
	 */
	if (PageHuge(p))
		set_page_hwpoison_huge_page(hpage);

	wait_on_page_writeback(p);

	/*
	 * Now take care of user space mappings.
	 * Abort on fail: __delete_from_page_cache() assumes unmapped page.
	 */
	if (hwpoison_user_mappings(p, pfn, trapno, flags) != SWAP_SUCCESS) {
		printk(KERN_ERR "MCE %#lx: cannot unmap page, give up\n", pfn);
		res = -EBUSY;
		goto out;
	}

	/*
	 * Torn down by someone else?
	 */
	if (PageLRU(p) && !PageSwapCache(p) && p->mapping == NULL) {
		action_result(pfn, "already truncated LRU", IGNORED);
		res = -EBUSY;
		goto out;
	}

	res = -EBUSY;
	/*
	 * The first check uses the current page flags which may not have any
	 * relevant information. The second check with the saved page flagss is
	 * carried out only if the first check can't determine the page status.
	 */
	for (ps = error_states;; ps++)
		if ((p->flags & ps->mask) == ps->res)
			break;

	page_flags |= (p->flags & (1UL << PG_dirty));

	if (!ps->mask)
		for (ps = error_states;; ps++)
			if ((page_flags & ps->mask) == ps->res)
				break;
	res = page_action(ps, p, pfn);
out:
	unlock_page(hpage);
	return res;
}
EXPORT_SYMBOL_GPL(memory_failure);

#define MEMORY_FAILURE_FIFO_ORDER	4
#define MEMORY_FAILURE_FIFO_SIZE	(1 << MEMORY_FAILURE_FIFO_ORDER)

struct memory_failure_entry {
	unsigned long pfn;
	int trapno;
	int flags;
};

struct memory_failure_cpu {
	DECLARE_KFIFO(fifo, struct memory_failure_entry,
		      MEMORY_FAILURE_FIFO_SIZE);
	spinlock_t lock;
	struct work_struct work;
};

static DEFINE_PER_CPU(struct memory_failure_cpu, memory_failure_cpu);

/**
 * memory_failure_queue - Schedule handling memory failure of a page.
 * @pfn: Page Number of the corrupted page
 * @trapno: Trap number reported in the signal to user space.
 * @flags: Flags for memory failure handling
 *
 * This function is called by the low level hardware error handler
 * when it detects hardware memory corruption of a page. It schedules
 * the recovering of error page, including dropping pages, killing
 * processes etc.
 *
 * The function is primarily of use for corruptions that
 * happen outside the current execution context (e.g. when
 * detected by a background scrubber)
 *
 * Can run in IRQ context.
 */
void memory_failure_queue(unsigned long pfn, int trapno, int flags)
{
	struct memory_failure_cpu *mf_cpu;
	unsigned long proc_flags;
	struct memory_failure_entry entry = {
		.pfn =		pfn,
		.trapno =	trapno,
		.flags =	flags,
	};

	mf_cpu = &get_cpu_var(memory_failure_cpu);
	spin_lock_irqsave(&mf_cpu->lock, proc_flags);
	if (kfifo_put(&mf_cpu->fifo, &entry))
		schedule_work_on(smp_processor_id(), &mf_cpu->work);
	else
		pr_err("Memory failure: buffer overflow when queuing memory failure at %#lx\n",
		       pfn);
	spin_unlock_irqrestore(&mf_cpu->lock, proc_flags);
	put_cpu_var(memory_failure_cpu);
}
EXPORT_SYMBOL_GPL(memory_failure_queue);

static void memory_failure_work_func(struct work_struct *work)
{
	struct memory_failure_cpu *mf_cpu;
	struct memory_failure_entry entry = { 0, };
	unsigned long proc_flags;
	int gotten;

	mf_cpu = &__get_cpu_var(memory_failure_cpu);
	for (;;) {
		spin_lock_irqsave(&mf_cpu->lock, proc_flags);
		gotten = kfifo_get(&mf_cpu->fifo, &entry);
		spin_unlock_irqrestore(&mf_cpu->lock, proc_flags);
		if (!gotten)
			break;
		if (entry.flags & MF_SOFT_OFFLINE)
			soft_offline_page(pfn_to_page(entry.pfn), entry.flags);
		else
			memory_failure(entry.pfn, entry.trapno, entry.flags);
	}
}

static int __init memory_failure_init(void)
{
	struct memory_failure_cpu *mf_cpu;
	int cpu;

	for_each_possible_cpu(cpu) {
		mf_cpu = &per_cpu(memory_failure_cpu, cpu);
		spin_lock_init(&mf_cpu->lock);
		INIT_KFIFO(mf_cpu->fifo);
		INIT_WORK(&mf_cpu->work, memory_failure_work_func);
	}

	return 0;
}
core_initcall(memory_failure_init);

/**
 * unpoison_memory - Unpoison a previously poisoned page
 * @pfn: Page number of the to be unpoisoned page
 *
 * Software-unpoison a page that has been poisoned by
 * memory_failure() earlier.
 *
 * This is only done on the software-level, so it only works
 * for linux injected failures, not real hardware failures
 *
 * Returns 0 for success, otherwise -errno.
 */
int unpoison_memory(unsigned long pfn)
{
	struct page *page;
	struct page *p;
	int freeit = 0;
	unsigned int nr_pages;

	if (!pfn_valid(pfn))
		return -ENXIO;

	p = pfn_to_page(pfn);
	page = compound_head(p);

	if (!PageHWPoison(p)) {
		pr_info("MCE: Page was already unpoisoned %#lx\n", pfn);
		return 0;
	}

	/*
	 * unpoison_memory() can encounter thp only when the thp is being
	 * worked by memory_failure() and the page lock is not held yet.
	 * In such case, we yield to memory_failure() and make unpoison fail.
	 */
	if (!PageHuge(page) && PageTransHuge(page)) {
		pr_info("MCE: Memory failure is now running on %#lx\n", pfn);
			return 0;
	}

	nr_pages = 1 << compound_order(page);

	if (!get_page_unless_zero(page)) {
		/*
		 * Since HWPoisoned hugepage should have non-zero refcount,
		 * race between memory failure and unpoison seems to happen.
		 * In such case unpoison fails and memory failure runs
		 * to the end.
		 */
		if (PageHuge(page)) {
			pr_info("MCE: Memory failure is now running on free hugepage %#lx\n", pfn);
			return 0;
		}
		if (TestClearPageHWPoison(p))
			atomic_long_dec(&num_poisoned_pages);
		pr_info("MCE: Software-unpoisoned free page %#lx\n", pfn);
		return 0;
	}

	lock_page(page);
	/*
	 * This test is racy because PG_hwpoison is set outside of page lock.
	 * That's acceptable because that won't trigger kernel panic. Instead,
	 * the PG_hwpoison page will be caught and isolated on the entrance to
	 * the free buddy page pool.
	 */
	if (TestClearPageHWPoison(page)) {
		pr_info("MCE: Software-unpoisoned page %#lx\n", pfn);
		atomic_long_sub(nr_pages, &num_poisoned_pages);
		freeit = 1;
		if (PageHuge(page))
			clear_page_hwpoison_huge_page(page);
	}
	unlock_page(page);

	put_page(page);
	if (freeit && !(pfn == my_zero_pfn(0) && page_count(p) == 1))
		put_page(page);

	return 0;
}
EXPORT_SYMBOL(unpoison_memory);

static struct page *new_page(struct page *p, unsigned long private, int **x)
{
	int nid = page_to_nid(p);
	if (PageHuge(p))
		return alloc_huge_page_node(page_hstate(compound_head(p)),
						   nid);
	else
		return alloc_pages_exact_node(nid, GFP_HIGHUSER_MOVABLE, 0);
}

/*
 * Safely get reference count of an arbitrary page.
 * Returns 0 for a free page, -EIO for a zero refcount page
 * that is not free, and 1 for any other page type.
 * For 1 the page is returned with increased page count, otherwise not.
 */
static int __get_any_page(struct page *p, unsigned long pfn, int flags)
{
	int ret;

	if (flags & MF_COUNT_INCREASED)
		return 1;

	/*
	 * When the target page is a free hugepage, just remove it
	 * from free hugepage list.
	 */
	if (!get_page_unless_zero(compound_head(p))) {
		if (PageHuge(p)) {
			pr_info("%s: %#lx free huge page\n", __func__, pfn);
			ret = 0;
		} else if (is_free_buddy_page(p)) {
			pr_info("%s: %#lx free buddy page\n", __func__, pfn);
			ret = 0;
		} else {
			pr_info("%s: %#lx: unknown zero refcount page type %lx\n",
				__func__, pfn, p->flags);
			ret = -EIO;
		}
	} else {
		/* Not a free page */
		ret = 1;
	}
	return ret;
}

static int get_any_page(struct page *page, unsigned long pfn, int flags)
{
	int ret = __get_any_page(page, pfn, flags);

	if (ret == 1 && !PageHuge(page) && !PageLRU(page)) {
		/*
		 * Try to free it.
		 */
		put_page(page);
		shake_page(page, 1);

		/*
		 * Did it turn free?
		 */
		ret = __get_any_page(page, pfn, 0);
		if (!PageLRU(page)) {
			pr_info("soft_offline: %#lx: unknown non LRU page type %lx\n",
				pfn, page->flags);
			return -EIO;
		}
	}
	return ret;
}

static int soft_offline_huge_page(struct page *page, int flags)
{
	int ret;
	unsigned long pfn = page_to_pfn(page);
	struct page *hpage = compound_head(page);
	LIST_HEAD(pagelist);

	/*
	 * This double-check of PageHWPoison is to avoid the race with
	 * memory_failure(). See also comment in __soft_offline_page().
	 */
	lock_page(hpage);
	if (PageHWPoison(hpage)) {
		unlock_page(hpage);
		put_page(hpage);
		pr_info("soft offline: %#lx hugepage already poisoned\n", pfn);
		return -EBUSY;
	}
	unlock_page(hpage);

	/* Keep page count to indicate a given hugepage is isolated. */
	list_move(&hpage->lru, &pagelist);
	ret = migrate_pages(&pagelist, new_page, MPOL_MF_MOVE_ALL,
				MIGRATE_SYNC, MR_MEMORY_FAILURE);
	if (ret) {
		pr_info("soft offline: %#lx: migration failed %d, type %lx\n",
			pfn, ret, page->flags);
		/*
		 * We know that soft_offline_huge_page() tries to migrate
		 * only one hugepage pointed to by hpage, so we need not
		 * run through the pagelist here.
		 */
		putback_active_hugepage(hpage);
		if (ret > 0)
			ret = -EIO;
	} else {
		set_page_hwpoison_huge_page(hpage);
		dequeue_hwpoisoned_huge_page(hpage);
		atomic_long_add(1 << compound_order(hpage),
				&num_poisoned_pages);
	}
	return ret;
}

static int __soft_offline_page(struct page *page, int flags)
{
	int ret;
	unsigned long pfn = page_to_pfn(page);

	/*
	 * Check PageHWPoison again inside page lock because PageHWPoison
	 * is set by memory_failure() outside page lock. Note that
	 * memory_failure() also double-checks PageHWPoison inside page lock,
	 * so there's no race between soft_offline_page() and memory_failure().
	 */
	lock_page(page);
	wait_on_page_writeback(page);
	if (PageHWPoison(page)) {
		unlock_page(page);
		put_page(page);
		pr_info("soft offline: %#lx page already poisoned\n", pfn);
		return -EBUSY;
	}
	/*
	 * Try to invalidate first. This should work for
	 * non dirty unmapped page cache pages.
	 */
	ret = invalidate_inode_page(page);
	unlock_page(page);
	/*
	 * RED-PEN would be better to keep it isolated here, but we
	 * would need to fix isolation locking first.
	 */
	if (ret == 1) {
		put_page(page);
		pr_info("soft_offline: %#lx: invalidated\n", pfn);
		SetPageHWPoison(page);
		atomic_long_inc(&num_poisoned_pages);
		return 0;
	}

	/*
	 * Simple invalidation didn't work.
	 * Try to migrate to a new page instead. migrate.c
	 * handles a large number of cases for us.
	 */
	ret = isolate_lru_page(page);
	/*
	 * Drop page reference which is came from get_any_page()
	 * successful isolate_lru_page() already took another one.
	 */
	put_page(page);
	if (!ret) {
		LIST_HEAD(pagelist);
		inc_zone_page_state(page, NR_ISOLATED_ANON +
					page_is_file_cache(page));
		list_add(&page->lru, &pagelist);
		ret = migrate_pages(&pagelist, new_page, MPOL_MF_MOVE_ALL,
					MIGRATE_SYNC, MR_MEMORY_FAILURE);
		if (ret) {
			putback_lru_pages(&pagelist);
			pr_info("soft offline: %#lx: migration failed %d, type %lx\n",
				pfn, ret, page->flags);
			if (ret > 0)
				ret = -EIO;
		} else {
			/*
			 * After page migration succeeds, the source page can
			 * be trapped in pagevec and actual freeing is delayed.
			 * Freeing code works differently based on PG_hwpoison,
			 * so there's a race. We need to make sure that the
			 * source page should be freed back to buddy before
			 * setting PG_hwpoison.
			 */
			if (!is_free_buddy_page(page))
				lru_add_drain_all();
			if (!is_free_buddy_page(page))
				drain_all_pages();
			SetPageHWPoison(page);
			if (!is_free_buddy_page(page))
				pr_info("soft offline: %#lx: page leaked\n",
					pfn);
			atomic_long_inc(&num_poisoned_pages);
		}
	} else {
		pr_info("soft offline: %#lx: isolation failed: %d, page count %d, type %lx\n",
			pfn, ret, page_count(page), page->flags);
	}
	return ret;
}

/**
 * soft_offline_page - Soft offline a page.
 * @page: page to offline
 * @flags: flags. Same as memory_failure().
 *
 * Returns 0 on success, otherwise negated errno.
 *
 * Soft offline a page, by migration or invalidation,
 * without killing anything. This is for the case when
 * a page is not corrupted yet (so it's still valid to access),
 * but has had a number of corrected errors and is better taken
 * out.
 *
 * The actual policy on when to do that is maintained by
 * user space.
 *
 * This should never impact any application or cause data loss,
 * however it might take some time.
 *
 * This is not a 100% solution for all memory, but tries to be
 * ``good enough'' for the majority of memory.
 */
int soft_offline_page(struct page *page, int flags)
{
	int ret;
	unsigned long pfn = page_to_pfn(page);
	struct page *hpage = compound_trans_head(page);

	if (PageHWPoison(page)) {
		pr_info("soft offline: %#lx page already poisoned\n", pfn);
		return -EBUSY;
	}
	if (!PageHuge(page) && PageTransHuge(hpage)) {
		if (PageAnon(hpage) && unlikely(split_huge_page(hpage))) {
			pr_info("soft offline: %#lx: failed to split THP\n",
				pfn);
			return -EBUSY;
		}
	}

	/*
	 * The lock_memory_hotplug prevents a race with memory hotplug.
	 * This is a big hammer, a better would be nicer.
	 */
	lock_memory_hotplug();

	/*
	 * Isolate the page, so that it doesn't get reallocated if it
	 * was free. This flag should be kept set until the source page
	 * is freed and PG_hwpoison on it is set.
	 */
	if (get_pageblock_migratetype(page) != MIGRATE_ISOLATE)
		set_migratetype_isolate(page, true);

	ret = get_any_page(page, pfn, flags);
	unlock_memory_hotplug();
	if (ret > 0) { /* for in-use pages */
		if (PageHuge(page))
			ret = soft_offline_huge_page(page, flags);
		else
			ret = __soft_offline_page(page, flags);
	} else if (ret == 0) { /* for free pages */
		if (PageHuge(page)) {
			set_page_hwpoison_huge_page(hpage);
			dequeue_hwpoisoned_huge_page(hpage);
			atomic_long_add(1 << compound_order(hpage),
					&num_poisoned_pages);
		} else {
			SetPageHWPoison(page);
			atomic_long_inc(&num_poisoned_pages);
		}
	}
	unset_migratetype_isolate(page, MIGRATE_MOVABLE);
	return ret;
}
