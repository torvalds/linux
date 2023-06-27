// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2008, 2009 Intel Corporation
 * Authors: Andi Kleen, Fengguang Wu
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
 * It can be very tempting to add handling for obscure cases here.
 * In general any code for handling new cases should only be added iff:
 * - You know how to test it.
 * - You have a test that can be added to mce-test
 *   https://git.kernel.org/cgit/utils/cpu/mce/mce-test.git/
 * - The case actually shows up as a frequent (top 10) page state in
 *   tools/mm/page-types when running a real workload.
 *
 * There are several operations here with exponential complexity because
 * of unsuitable VM data structures. For example the operation to map back
 * from RMAP chains to processes has to walk the complete process list and
 * has non linear complexity with the number. But since memory corruptions
 * are rare we hope to get away with this. This avoids impacting the core
 * VM.
 */

#define pr_fmt(fmt) "Memory failure: " fmt

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/page-flags.h>
#include <linux/kernel-page-flags.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/dax.h>
#include <linux/ksm.h>
#include <linux/rmap.h>
#include <linux/export.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/backing-dev.h>
#include <linux/migrate.h>
#include <linux/suspend.h>
#include <linux/slab.h>
#include <linux/swapops.h>
#include <linux/hugetlb.h>
#include <linux/memory_hotplug.h>
#include <linux/mm_inline.h>
#include <linux/memremap.h>
#include <linux/kfifo.h>
#include <linux/ratelimit.h>
#include <linux/page-isolation.h>
#include <linux/pagewalk.h>
#include <linux/shmem_fs.h>
#include <linux/sysctl.h>
#include "swap.h"
#include "internal.h"
#include "ras/ras_event.h"

static int sysctl_memory_failure_early_kill __read_mostly;

static int sysctl_memory_failure_recovery __read_mostly = 1;

atomic_long_t num_poisoned_pages __read_mostly = ATOMIC_LONG_INIT(0);

static bool hw_memory_failure __read_mostly = false;

inline void num_poisoned_pages_inc(unsigned long pfn)
{
	atomic_long_inc(&num_poisoned_pages);
	memblk_nr_poison_inc(pfn);
}

inline void num_poisoned_pages_sub(unsigned long pfn, long i)
{
	atomic_long_sub(i, &num_poisoned_pages);
	if (pfn != -1UL)
		memblk_nr_poison_sub(pfn, i);
}

/**
 * MF_ATTR_RO - Create sysfs entry for each memory failure statistics.
 * @_name: name of the file in the per NUMA sysfs directory.
 */
#define MF_ATTR_RO(_name)					\
static ssize_t _name##_show(struct device *dev,			\
			    struct device_attribute *attr,	\
			    char *buf)				\
{								\
	struct memory_failure_stats *mf_stats =			\
		&NODE_DATA(dev->id)->mf_stats;			\
	return sprintf(buf, "%lu\n", mf_stats->_name);		\
}								\
static DEVICE_ATTR_RO(_name)

MF_ATTR_RO(total);
MF_ATTR_RO(ignored);
MF_ATTR_RO(failed);
MF_ATTR_RO(delayed);
MF_ATTR_RO(recovered);

static struct attribute *memory_failure_attr[] = {
	&dev_attr_total.attr,
	&dev_attr_ignored.attr,
	&dev_attr_failed.attr,
	&dev_attr_delayed.attr,
	&dev_attr_recovered.attr,
	NULL,
};

const struct attribute_group memory_failure_attr_group = {
	.name = "memory_failure",
	.attrs = memory_failure_attr,
};

static struct ctl_table memory_failure_table[] = {
	{
		.procname	= "memory_failure_early_kill",
		.data		= &sysctl_memory_failure_early_kill,
		.maxlen		= sizeof(sysctl_memory_failure_early_kill),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
	{
		.procname	= "memory_failure_recovery",
		.data		= &sysctl_memory_failure_recovery,
		.maxlen		= sizeof(sysctl_memory_failure_recovery),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
	{ }
};

/*
 * Return values:
 *   1:   the page is dissolved (if needed) and taken off from buddy,
 *   0:   the page is dissolved (if needed) and not taken off from buddy,
 *   < 0: failed to dissolve.
 */
static int __page_handle_poison(struct page *page)
{
	int ret;

	zone_pcp_disable(page_zone(page));
	ret = dissolve_free_huge_page(page);
	if (!ret)
		ret = take_page_off_buddy(page);
	zone_pcp_enable(page_zone(page));

	return ret;
}

static bool page_handle_poison(struct page *page, bool hugepage_or_freepage, bool release)
{
	if (hugepage_or_freepage) {
		/*
		 * Doing this check for free pages is also fine since dissolve_free_huge_page
		 * returns 0 for non-hugetlb pages as well.
		 */
		if (__page_handle_poison(page) <= 0)
			/*
			 * We could fail to take off the target page from buddy
			 * for example due to racy page allocation, but that's
			 * acceptable because soft-offlined page is not broken
			 * and if someone really want to use it, they should
			 * take it.
			 */
			return false;
	}

	SetPageHWPoison(page);
	if (release)
		put_page(page);
	page_ref_inc(page);
	num_poisoned_pages_inc(page_to_pfn(page));

	return true;
}

#if IS_ENABLED(CONFIG_HWPOISON_INJECT)

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
#ifdef CONFIG_MEMCG
u64 hwpoison_filter_memcg;
EXPORT_SYMBOL_GPL(hwpoison_filter_memcg);
static int hwpoison_filter_task(struct page *p)
{
	if (!hwpoison_filter_memcg)
		return 0;

	if (page_cgroup_ino(p) != hwpoison_filter_memcg)
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
	short size_shift;
};

/*
 * Send all the processes who have the page mapped a signal.
 * ``action optional'' if they are not immediately affected by the error
 * ``action required'' if error happened in current execution context
 */
static int kill_proc(struct to_kill *tk, unsigned long pfn, int flags)
{
	struct task_struct *t = tk->tsk;
	short addr_lsb = tk->size_shift;
	int ret = 0;

	pr_err("%#lx: Sending SIGBUS to %s:%d due to hardware memory corruption\n",
			pfn, t->comm, t->pid);

	if ((flags & MF_ACTION_REQUIRED) && (t == current))
		ret = force_sig_mceerr(BUS_MCEERR_AR,
				 (void __user *)tk->addr, addr_lsb);
	else
		/*
		 * Signal other processes sharing the page if they have
		 * PF_MCE_EARLY set.
		 * Don't use force here, it's convenient if the signal
		 * can be temporarily blocked.
		 * This could cause a loop when the user sets SIGBUS
		 * to SIG_IGN, but hopefully no one will do that?
		 */
		ret = send_sig_mceerr(BUS_MCEERR_AO, (void __user *)tk->addr,
				      addr_lsb, t);
	if (ret < 0)
		pr_info("Error sending signal to %s:%d: %d\n",
			t->comm, t->pid, ret);
	return ret;
}

/*
 * Unknown page type encountered. Try to check whether it can turn PageLRU by
 * lru_add_drain_all.
 */
void shake_page(struct page *p)
{
	if (PageHuge(p))
		return;

	if (!PageSlab(p)) {
		lru_add_drain_all();
		if (PageLRU(p) || is_free_buddy_page(p))
			return;
	}

	/*
	 * TODO: Could shrink slab caches here if a lightweight range-based
	 * shrinker will be available.
	 */
}
EXPORT_SYMBOL_GPL(shake_page);

static unsigned long dev_pagemap_mapping_shift(struct vm_area_struct *vma,
		unsigned long address)
{
	unsigned long ret = 0;
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	pte_t ptent;

	VM_BUG_ON_VMA(address == -EFAULT, vma);
	pgd = pgd_offset(vma->vm_mm, address);
	if (!pgd_present(*pgd))
		return 0;
	p4d = p4d_offset(pgd, address);
	if (!p4d_present(*p4d))
		return 0;
	pud = pud_offset(p4d, address);
	if (!pud_present(*pud))
		return 0;
	if (pud_devmap(*pud))
		return PUD_SHIFT;
	pmd = pmd_offset(pud, address);
	if (!pmd_present(*pmd))
		return 0;
	if (pmd_devmap(*pmd))
		return PMD_SHIFT;
	pte = pte_offset_map(pmd, address);
	if (!pte)
		return 0;
	ptent = ptep_get(pte);
	if (pte_present(ptent) && pte_devmap(ptent))
		ret = PAGE_SHIFT;
	pte_unmap(pte);
	return ret;
}

/*
 * Failure handling: if we can't find or can't kill a process there's
 * not much we can do.	We just print a message and ignore otherwise.
 */

#define FSDAX_INVALID_PGOFF ULONG_MAX

/*
 * Schedule a process for later kill.
 * Uses GFP_ATOMIC allocations to avoid potential recursions in the VM.
 *
 * Note: @fsdax_pgoff is used only when @p is a fsdax page and a
 * filesystem with a memory failure handler has claimed the
 * memory_failure event. In all other cases, page->index and
 * page->mapping are sufficient for mapping the page back to its
 * corresponding user virtual address.
 */
static void __add_to_kill(struct task_struct *tsk, struct page *p,
			  struct vm_area_struct *vma, struct list_head *to_kill,
			  unsigned long ksm_addr, pgoff_t fsdax_pgoff)
{
	struct to_kill *tk;

	tk = kmalloc(sizeof(struct to_kill), GFP_ATOMIC);
	if (!tk) {
		pr_err("Out of memory while machine check handling\n");
		return;
	}

	tk->addr = ksm_addr ? ksm_addr : page_address_in_vma(p, vma);
	if (is_zone_device_page(p)) {
		if (fsdax_pgoff != FSDAX_INVALID_PGOFF)
			tk->addr = vma_pgoff_address(fsdax_pgoff, 1, vma);
		tk->size_shift = dev_pagemap_mapping_shift(vma, tk->addr);
	} else
		tk->size_shift = page_shift(compound_head(p));

	/*
	 * Send SIGKILL if "tk->addr == -EFAULT". Also, as
	 * "tk->size_shift" is always non-zero for !is_zone_device_page(),
	 * so "tk->size_shift == 0" effectively checks no mapping on
	 * ZONE_DEVICE. Indeed, when a devdax page is mmapped N times
	 * to a process' address space, it's possible not all N VMAs
	 * contain mappings for the page, but at least one VMA does.
	 * Only deliver SIGBUS with payload derived from the VMA that
	 * has a mapping for the page.
	 */
	if (tk->addr == -EFAULT) {
		pr_info("Unable to find user space address %lx in %s\n",
			page_to_pfn(p), tsk->comm);
	} else if (tk->size_shift == 0) {
		kfree(tk);
		return;
	}

	get_task_struct(tsk);
	tk->tsk = tsk;
	list_add_tail(&tk->nd, to_kill);
}

static void add_to_kill_anon_file(struct task_struct *tsk, struct page *p,
				  struct vm_area_struct *vma,
				  struct list_head *to_kill)
{
	__add_to_kill(tsk, p, vma, to_kill, 0, FSDAX_INVALID_PGOFF);
}

#ifdef CONFIG_KSM
static bool task_in_to_kill_list(struct list_head *to_kill,
				 struct task_struct *tsk)
{
	struct to_kill *tk, *next;

	list_for_each_entry_safe(tk, next, to_kill, nd) {
		if (tk->tsk == tsk)
			return true;
	}

	return false;
}
void add_to_kill_ksm(struct task_struct *tsk, struct page *p,
		     struct vm_area_struct *vma, struct list_head *to_kill,
		     unsigned long ksm_addr)
{
	if (!task_in_to_kill_list(to_kill, tsk))
		__add_to_kill(tsk, p, vma, to_kill, ksm_addr, FSDAX_INVALID_PGOFF);
}
#endif
/*
 * Kill the processes that have been collected earlier.
 *
 * Only do anything when FORCEKILL is set, otherwise just free the
 * list (this is used for clean pages which do not need killing)
 * Also when FAIL is set do a force kill because something went
 * wrong earlier.
 */
static void kill_procs(struct list_head *to_kill, int forcekill, bool fail,
		unsigned long pfn, int flags)
{
	struct to_kill *tk, *next;

	list_for_each_entry_safe(tk, next, to_kill, nd) {
		if (forcekill) {
			/*
			 * In case something went wrong with munmapping
			 * make sure the process doesn't catch the
			 * signal and then access the memory. Just kill it.
			 */
			if (fail || tk->addr == -EFAULT) {
				pr_err("%#lx: forcibly killing %s:%d because of failure to unmap corrupted page\n",
				       pfn, tk->tsk->comm, tk->tsk->pid);
				do_send_sig_info(SIGKILL, SEND_SIG_PRIV,
						 tk->tsk, PIDTYPE_PID);
			}

			/*
			 * In theory the process could have mapped
			 * something else on the address in-between. We could
			 * check for that, but we need to tell the
			 * process anyways.
			 */
			else if (kill_proc(tk, pfn, flags) < 0)
				pr_err("%#lx: Cannot send advisory machine check signal to %s:%d\n",
				       pfn, tk->tsk->comm, tk->tsk->pid);
		}
		list_del(&tk->nd);
		put_task_struct(tk->tsk);
		kfree(tk);
	}
}

/*
 * Find a dedicated thread which is supposed to handle SIGBUS(BUS_MCEERR_AO)
 * on behalf of the thread group. Return task_struct of the (first found)
 * dedicated thread if found, and return NULL otherwise.
 *
 * We already hold read_lock(&tasklist_lock) in the caller, so we don't
 * have to call rcu_read_lock/unlock() in this function.
 */
static struct task_struct *find_early_kill_thread(struct task_struct *tsk)
{
	struct task_struct *t;

	for_each_thread(tsk, t) {
		if (t->flags & PF_MCE_PROCESS) {
			if (t->flags & PF_MCE_EARLY)
				return t;
		} else {
			if (sysctl_memory_failure_early_kill)
				return t;
		}
	}
	return NULL;
}

/*
 * Determine whether a given process is "early kill" process which expects
 * to be signaled when some page under the process is hwpoisoned.
 * Return task_struct of the dedicated thread (main thread unless explicitly
 * specified) if the process is "early kill" and otherwise returns NULL.
 *
 * Note that the above is true for Action Optional case. For Action Required
 * case, it's only meaningful to the current thread which need to be signaled
 * with SIGBUS, this error is Action Optional for other non current
 * processes sharing the same error page,if the process is "early kill", the
 * task_struct of the dedicated thread will also be returned.
 */
struct task_struct *task_early_kill(struct task_struct *tsk, int force_early)
{
	if (!tsk->mm)
		return NULL;
	/*
	 * Comparing ->mm here because current task might represent
	 * a subthread, while tsk always points to the main thread.
	 */
	if (force_early && tsk->mm == current->mm)
		return current;

	return find_early_kill_thread(tsk);
}

/*
 * Collect processes when the error hit an anonymous page.
 */
static void collect_procs_anon(struct page *page, struct list_head *to_kill,
				int force_early)
{
	struct folio *folio = page_folio(page);
	struct vm_area_struct *vma;
	struct task_struct *tsk;
	struct anon_vma *av;
	pgoff_t pgoff;

	av = folio_lock_anon_vma_read(folio, NULL);
	if (av == NULL)	/* Not actually mapped anymore */
		return;

	pgoff = page_to_pgoff(page);
	read_lock(&tasklist_lock);
	for_each_process (tsk) {
		struct anon_vma_chain *vmac;
		struct task_struct *t = task_early_kill(tsk, force_early);

		if (!t)
			continue;
		anon_vma_interval_tree_foreach(vmac, &av->rb_root,
					       pgoff, pgoff) {
			vma = vmac->vma;
			if (vma->vm_mm != t->mm)
				continue;
			if (!page_mapped_in_vma(page, vma))
				continue;
			add_to_kill_anon_file(t, page, vma, to_kill);
		}
	}
	read_unlock(&tasklist_lock);
	anon_vma_unlock_read(av);
}

/*
 * Collect processes when the error hit a file mapped page.
 */
static void collect_procs_file(struct page *page, struct list_head *to_kill,
				int force_early)
{
	struct vm_area_struct *vma;
	struct task_struct *tsk;
	struct address_space *mapping = page->mapping;
	pgoff_t pgoff;

	i_mmap_lock_read(mapping);
	read_lock(&tasklist_lock);
	pgoff = page_to_pgoff(page);
	for_each_process(tsk) {
		struct task_struct *t = task_early_kill(tsk, force_early);

		if (!t)
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
			if (vma->vm_mm == t->mm)
				add_to_kill_anon_file(t, page, vma, to_kill);
		}
	}
	read_unlock(&tasklist_lock);
	i_mmap_unlock_read(mapping);
}

#ifdef CONFIG_FS_DAX
static void add_to_kill_fsdax(struct task_struct *tsk, struct page *p,
			      struct vm_area_struct *vma,
			      struct list_head *to_kill, pgoff_t pgoff)
{
	__add_to_kill(tsk, p, vma, to_kill, 0, pgoff);
}

/*
 * Collect processes when the error hit a fsdax page.
 */
static void collect_procs_fsdax(struct page *page,
		struct address_space *mapping, pgoff_t pgoff,
		struct list_head *to_kill)
{
	struct vm_area_struct *vma;
	struct task_struct *tsk;

	i_mmap_lock_read(mapping);
	read_lock(&tasklist_lock);
	for_each_process(tsk) {
		struct task_struct *t = task_early_kill(tsk, true);

		if (!t)
			continue;
		vma_interval_tree_foreach(vma, &mapping->i_mmap, pgoff, pgoff) {
			if (vma->vm_mm == t->mm)
				add_to_kill_fsdax(t, page, vma, to_kill, pgoff);
		}
	}
	read_unlock(&tasklist_lock);
	i_mmap_unlock_read(mapping);
}
#endif /* CONFIG_FS_DAX */

/*
 * Collect the processes who have the corrupted page mapped to kill.
 */
static void collect_procs(struct page *page, struct list_head *tokill,
				int force_early)
{
	if (!page->mapping)
		return;
	if (unlikely(PageKsm(page)))
		collect_procs_ksm(page, tokill, force_early);
	else if (PageAnon(page))
		collect_procs_anon(page, tokill, force_early);
	else
		collect_procs_file(page, tokill, force_early);
}

struct hwp_walk {
	struct to_kill tk;
	unsigned long pfn;
	int flags;
};

static void set_to_kill(struct to_kill *tk, unsigned long addr, short shift)
{
	tk->addr = addr;
	tk->size_shift = shift;
}

static int check_hwpoisoned_entry(pte_t pte, unsigned long addr, short shift,
				unsigned long poisoned_pfn, struct to_kill *tk)
{
	unsigned long pfn = 0;

	if (pte_present(pte)) {
		pfn = pte_pfn(pte);
	} else {
		swp_entry_t swp = pte_to_swp_entry(pte);

		if (is_hwpoison_entry(swp))
			pfn = swp_offset_pfn(swp);
	}

	if (!pfn || pfn != poisoned_pfn)
		return 0;

	set_to_kill(tk, addr, shift);
	return 1;
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static int check_hwpoisoned_pmd_entry(pmd_t *pmdp, unsigned long addr,
				      struct hwp_walk *hwp)
{
	pmd_t pmd = *pmdp;
	unsigned long pfn;
	unsigned long hwpoison_vaddr;

	if (!pmd_present(pmd))
		return 0;
	pfn = pmd_pfn(pmd);
	if (pfn <= hwp->pfn && hwp->pfn < pfn + HPAGE_PMD_NR) {
		hwpoison_vaddr = addr + ((hwp->pfn - pfn) << PAGE_SHIFT);
		set_to_kill(&hwp->tk, hwpoison_vaddr, PAGE_SHIFT);
		return 1;
	}
	return 0;
}
#else
static int check_hwpoisoned_pmd_entry(pmd_t *pmdp, unsigned long addr,
				      struct hwp_walk *hwp)
{
	return 0;
}
#endif

static int hwpoison_pte_range(pmd_t *pmdp, unsigned long addr,
			      unsigned long end, struct mm_walk *walk)
{
	struct hwp_walk *hwp = walk->private;
	int ret = 0;
	pte_t *ptep, *mapped_pte;
	spinlock_t *ptl;

	ptl = pmd_trans_huge_lock(pmdp, walk->vma);
	if (ptl) {
		ret = check_hwpoisoned_pmd_entry(pmdp, addr, hwp);
		spin_unlock(ptl);
		goto out;
	}

	mapped_pte = ptep = pte_offset_map_lock(walk->vma->vm_mm, pmdp,
						addr, &ptl);
	if (!ptep)
		goto out;

	for (; addr != end; ptep++, addr += PAGE_SIZE) {
		ret = check_hwpoisoned_entry(ptep_get(ptep), addr, PAGE_SHIFT,
					     hwp->pfn, &hwp->tk);
		if (ret == 1)
			break;
	}
	pte_unmap_unlock(mapped_pte, ptl);
out:
	cond_resched();
	return ret;
}

#ifdef CONFIG_HUGETLB_PAGE
static int hwpoison_hugetlb_range(pte_t *ptep, unsigned long hmask,
			    unsigned long addr, unsigned long end,
			    struct mm_walk *walk)
{
	struct hwp_walk *hwp = walk->private;
	pte_t pte = huge_ptep_get(ptep);
	struct hstate *h = hstate_vma(walk->vma);

	return check_hwpoisoned_entry(pte, addr, huge_page_shift(h),
				      hwp->pfn, &hwp->tk);
}
#else
#define hwpoison_hugetlb_range	NULL
#endif

static const struct mm_walk_ops hwp_walk_ops = {
	.pmd_entry = hwpoison_pte_range,
	.hugetlb_entry = hwpoison_hugetlb_range,
	.walk_lock = PGWALK_RDLOCK,
};

/*
 * Sends SIGBUS to the current process with error info.
 *
 * This function is intended to handle "Action Required" MCEs on already
 * hardware poisoned pages. They could happen, for example, when
 * memory_failure() failed to unmap the error page at the first call, or
 * when multiple local machine checks happened on different CPUs.
 *
 * MCE handler currently has no easy access to the error virtual address,
 * so this function walks page table to find it. The returned virtual address
 * is proper in most cases, but it could be wrong when the application
 * process has multiple entries mapping the error page.
 */
static int kill_accessing_process(struct task_struct *p, unsigned long pfn,
				  int flags)
{
	int ret;
	struct hwp_walk priv = {
		.pfn = pfn,
	};
	priv.tk.tsk = p;

	if (!p->mm)
		return -EFAULT;

	mmap_read_lock(p->mm);
	ret = walk_page_range(p->mm, 0, TASK_SIZE, &hwp_walk_ops,
			      (void *)&priv);
	if (ret == 1 && priv.tk.addr)
		kill_proc(&priv.tk, pfn, flags);
	else
		ret = 0;
	mmap_read_unlock(p->mm);
	return ret > 0 ? -EHWPOISON : -EFAULT;
}

static const char *action_name[] = {
	[MF_IGNORED] = "Ignored",
	[MF_FAILED] = "Failed",
	[MF_DELAYED] = "Delayed",
	[MF_RECOVERED] = "Recovered",
};

static const char * const action_page_types[] = {
	[MF_MSG_KERNEL]			= "reserved kernel page",
	[MF_MSG_KERNEL_HIGH_ORDER]	= "high-order kernel page",
	[MF_MSG_SLAB]			= "kernel slab page",
	[MF_MSG_DIFFERENT_COMPOUND]	= "different compound page after locking",
	[MF_MSG_HUGE]			= "huge page",
	[MF_MSG_FREE_HUGE]		= "free huge page",
	[MF_MSG_UNMAP_FAILED]		= "unmapping failed page",
	[MF_MSG_DIRTY_SWAPCACHE]	= "dirty swapcache page",
	[MF_MSG_CLEAN_SWAPCACHE]	= "clean swapcache page",
	[MF_MSG_DIRTY_MLOCKED_LRU]	= "dirty mlocked LRU page",
	[MF_MSG_CLEAN_MLOCKED_LRU]	= "clean mlocked LRU page",
	[MF_MSG_DIRTY_UNEVICTABLE_LRU]	= "dirty unevictable LRU page",
	[MF_MSG_CLEAN_UNEVICTABLE_LRU]	= "clean unevictable LRU page",
	[MF_MSG_DIRTY_LRU]		= "dirty LRU page",
	[MF_MSG_CLEAN_LRU]		= "clean LRU page",
	[MF_MSG_TRUNCATED_LRU]		= "already truncated LRU page",
	[MF_MSG_BUDDY]			= "free buddy page",
	[MF_MSG_DAX]			= "dax page",
	[MF_MSG_UNSPLIT_THP]		= "unsplit thp",
	[MF_MSG_UNKNOWN]		= "unknown page",
};

/*
 * XXX: It is possible that a page is isolated from LRU cache,
 * and then kept in swap cache or failed to remove from page cache.
 * The page count will stop it from being freed by unpoison.
 * Stress tests should be aware of this memory leak problem.
 */
static int delete_from_lru_cache(struct page *p)
{
	if (isolate_lru_page(p)) {
		/*
		 * Clear sensible page flags, so that the buddy system won't
		 * complain when the page is unpoison-and-freed.
		 */
		ClearPageActive(p);
		ClearPageUnevictable(p);

		/*
		 * Poisoned page might never drop its ref count to 0 so we have
		 * to uncharge it manually from its memcg.
		 */
		mem_cgroup_uncharge(page_folio(p));

		/*
		 * drop the page count elevated by isolate_lru_page()
		 */
		put_page(p);
		return 0;
	}
	return -EIO;
}

static int truncate_error_page(struct page *p, unsigned long pfn,
				struct address_space *mapping)
{
	int ret = MF_FAILED;

	if (mapping->a_ops->error_remove_page) {
		struct folio *folio = page_folio(p);
		int err = mapping->a_ops->error_remove_page(mapping, p);

		if (err != 0) {
			pr_info("%#lx: Failed to punch page: %d\n", pfn, err);
		} else if (folio_has_private(folio) &&
			   !filemap_release_folio(folio, GFP_NOIO)) {
			pr_info("%#lx: failed to release buffers\n", pfn);
		} else {
			ret = MF_RECOVERED;
		}
	} else {
		/*
		 * If the file system doesn't support it just invalidate
		 * This fails on dirty or anything with private pages
		 */
		if (invalidate_inode_page(p))
			ret = MF_RECOVERED;
		else
			pr_info("%#lx: Failed to invalidate\n",	pfn);
	}

	return ret;
}

struct page_state {
	unsigned long mask;
	unsigned long res;
	enum mf_action_page_type type;

	/* Callback ->action() has to unlock the relevant page inside it. */
	int (*action)(struct page_state *ps, struct page *p);
};

/*
 * Return true if page is still referenced by others, otherwise return
 * false.
 *
 * The extra_pins is true when one extra refcount is expected.
 */
static bool has_extra_refcount(struct page_state *ps, struct page *p,
			       bool extra_pins)
{
	int count = page_count(p) - 1;

	if (extra_pins)
		count -= 1;

	if (count > 0) {
		pr_err("%#lx: %s still referenced by %d users\n",
		       page_to_pfn(p), action_page_types[ps->type], count);
		return true;
	}

	return false;
}

/*
 * Error hit kernel page.
 * Do nothing, try to be lucky and not touch this instead. For a few cases we
 * could be more sophisticated.
 */
static int me_kernel(struct page_state *ps, struct page *p)
{
	unlock_page(p);
	return MF_IGNORED;
}

/*
 * Page in unknown state. Do nothing.
 */
static int me_unknown(struct page_state *ps, struct page *p)
{
	pr_err("%#lx: Unknown page state\n", page_to_pfn(p));
	unlock_page(p);
	return MF_FAILED;
}

/*
 * Clean (or cleaned) page cache page.
 */
static int me_pagecache_clean(struct page_state *ps, struct page *p)
{
	int ret;
	struct address_space *mapping;
	bool extra_pins;

	delete_from_lru_cache(p);

	/*
	 * For anonymous pages we're done the only reference left
	 * should be the one m_f() holds.
	 */
	if (PageAnon(p)) {
		ret = MF_RECOVERED;
		goto out;
	}

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
		ret = MF_FAILED;
		goto out;
	}

	/*
	 * The shmem page is kept in page cache instead of truncating
	 * so is expected to have an extra refcount after error-handling.
	 */
	extra_pins = shmem_mapping(mapping);

	/*
	 * Truncation is a bit tricky. Enable it per file system for now.
	 *
	 * Open: to take i_rwsem or not for this? Right now we don't.
	 */
	ret = truncate_error_page(p, page_to_pfn(p), mapping);
	if (has_extra_refcount(ps, p, extra_pins))
		ret = MF_FAILED;

out:
	unlock_page(p);

	return ret;
}

/*
 * Dirty pagecache page
 * Issues: when the error hit a hole page the error is not properly
 * propagated.
 */
static int me_pagecache_dirty(struct page_state *ps, struct page *p)
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
		mapping_set_error(mapping, -EIO);
	}

	return me_pagecache_clean(ps, p);
}

/*
 * Clean and dirty swap cache.
 *
 * Dirty swap cache page is tricky to handle. The page could live both in page
 * cache and swap cache(ie. page is freshly swapped in). So it could be
 * referenced concurrently by 2 types of PTEs:
 * normal PTEs and swap PTEs. We try to handle them consistently by calling
 * try_to_unmap(!TTU_HWPOISON) to convert the normal PTEs to swap PTEs,
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
static int me_swapcache_dirty(struct page_state *ps, struct page *p)
{
	int ret;
	bool extra_pins = false;

	ClearPageDirty(p);
	/* Trigger EIO in shmem: */
	ClearPageUptodate(p);

	ret = delete_from_lru_cache(p) ? MF_FAILED : MF_DELAYED;
	unlock_page(p);

	if (ret == MF_DELAYED)
		extra_pins = true;

	if (has_extra_refcount(ps, p, extra_pins))
		ret = MF_FAILED;

	return ret;
}

static int me_swapcache_clean(struct page_state *ps, struct page *p)
{
	struct folio *folio = page_folio(p);
	int ret;

	delete_from_swap_cache(folio);

	ret = delete_from_lru_cache(p) ? MF_FAILED : MF_RECOVERED;
	folio_unlock(folio);

	if (has_extra_refcount(ps, p, false))
		ret = MF_FAILED;

	return ret;
}

/*
 * Huge pages. Needs work.
 * Issues:
 * - Error on hugepage is contained in hugepage unit (not in raw page unit.)
 *   To narrow down kill region to one page, we need to break up pmd.
 */
static int me_huge_page(struct page_state *ps, struct page *p)
{
	int res;
	struct page *hpage = compound_head(p);
	struct address_space *mapping;
	bool extra_pins = false;

	if (!PageHuge(hpage))
		return MF_DELAYED;

	mapping = page_mapping(hpage);
	if (mapping) {
		res = truncate_error_page(hpage, page_to_pfn(p), mapping);
		/* The page is kept in page cache. */
		extra_pins = true;
		unlock_page(hpage);
	} else {
		unlock_page(hpage);
		/*
		 * migration entry prevents later access on error hugepage,
		 * so we can free and dissolve it into buddy to save healthy
		 * subpages.
		 */
		put_page(hpage);
		if (__page_handle_poison(p) >= 0) {
			page_ref_inc(p);
			res = MF_RECOVERED;
		} else {
			res = MF_FAILED;
		}
	}

	if (has_extra_refcount(ps, p, extra_pins))
		res = MF_FAILED;

	return res;
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
#define sc		((1UL << PG_swapcache) | (1UL << PG_swapbacked))
#define unevict		(1UL << PG_unevictable)
#define mlock		(1UL << PG_mlocked)
#define lru		(1UL << PG_lru)
#define head		(1UL << PG_head)
#define slab		(1UL << PG_slab)
#define reserved	(1UL << PG_reserved)

static struct page_state error_states[] = {
	{ reserved,	reserved,	MF_MSG_KERNEL,	me_kernel },
	/*
	 * free pages are specially detected outside this table:
	 * PG_buddy pages only make a small fraction of all free pages.
	 */

	/*
	 * Could in theory check if slab page is free or if we can drop
	 * currently unused objects without touching them. But just
	 * treat it as standard kernel for now.
	 */
	{ slab,		slab,		MF_MSG_SLAB,	me_kernel },

	{ head,		head,		MF_MSG_HUGE,		me_huge_page },

	{ sc|dirty,	sc|dirty,	MF_MSG_DIRTY_SWAPCACHE,	me_swapcache_dirty },
	{ sc|dirty,	sc,		MF_MSG_CLEAN_SWAPCACHE,	me_swapcache_clean },

	{ mlock|dirty,	mlock|dirty,	MF_MSG_DIRTY_MLOCKED_LRU,	me_pagecache_dirty },
	{ mlock|dirty,	mlock,		MF_MSG_CLEAN_MLOCKED_LRU,	me_pagecache_clean },

	{ unevict|dirty, unevict|dirty,	MF_MSG_DIRTY_UNEVICTABLE_LRU,	me_pagecache_dirty },
	{ unevict|dirty, unevict,	MF_MSG_CLEAN_UNEVICTABLE_LRU,	me_pagecache_clean },

	{ lru|dirty,	lru|dirty,	MF_MSG_DIRTY_LRU,	me_pagecache_dirty },
	{ lru|dirty,	lru,		MF_MSG_CLEAN_LRU,	me_pagecache_clean },

	/*
	 * Catchall entry: must be at end.
	 */
	{ 0,		0,		MF_MSG_UNKNOWN,	me_unknown },
};

#undef dirty
#undef sc
#undef unevict
#undef mlock
#undef lru
#undef head
#undef slab
#undef reserved

static void update_per_node_mf_stats(unsigned long pfn,
				     enum mf_result result)
{
	int nid = MAX_NUMNODES;
	struct memory_failure_stats *mf_stats = NULL;

	nid = pfn_to_nid(pfn);
	if (unlikely(nid < 0 || nid >= MAX_NUMNODES)) {
		WARN_ONCE(1, "Memory failure: pfn=%#lx, invalid nid=%d", pfn, nid);
		return;
	}

	mf_stats = &NODE_DATA(nid)->mf_stats;
	switch (result) {
	case MF_IGNORED:
		++mf_stats->ignored;
		break;
	case MF_FAILED:
		++mf_stats->failed;
		break;
	case MF_DELAYED:
		++mf_stats->delayed;
		break;
	case MF_RECOVERED:
		++mf_stats->recovered;
		break;
	default:
		WARN_ONCE(1, "Memory failure: mf_result=%d is not properly handled", result);
		break;
	}
	++mf_stats->total;
}

/*
 * "Dirty/Clean" indication is not 100% accurate due to the possibility of
 * setting PG_dirty outside page lock. See also comment above set_page_dirty().
 */
static int action_result(unsigned long pfn, enum mf_action_page_type type,
			 enum mf_result result)
{
	trace_memory_failure_event(pfn, type, result);

	num_poisoned_pages_inc(pfn);

	update_per_node_mf_stats(pfn, result);

	pr_err("%#lx: recovery action for %s: %s\n",
		pfn, action_page_types[type], action_name[result]);

	return (result == MF_RECOVERED || result == MF_DELAYED) ? 0 : -EBUSY;
}

static int page_action(struct page_state *ps, struct page *p,
			unsigned long pfn)
{
	int result;

	/* page p should be unlocked after returning from ps->action().  */
	result = ps->action(ps, p);

	/* Could do more checks here if page looks ok */
	/*
	 * Could adjust zone counters here to correct for the missing page.
	 */

	return action_result(pfn, ps->type, result);
}

static inline bool PageHWPoisonTakenOff(struct page *page)
{
	return PageHWPoison(page) && page_private(page) == MAGIC_HWPOISON;
}

void SetPageHWPoisonTakenOff(struct page *page)
{
	set_page_private(page, MAGIC_HWPOISON);
}

void ClearPageHWPoisonTakenOff(struct page *page)
{
	if (PageHWPoison(page))
		set_page_private(page, 0);
}

/*
 * Return true if a page type of a given page is supported by hwpoison
 * mechanism (while handling could fail), otherwise false.  This function
 * does not return true for hugetlb or device memory pages, so it's assumed
 * to be called only in the context where we never have such pages.
 */
static inline bool HWPoisonHandlable(struct page *page, unsigned long flags)
{
	/* Soft offline could migrate non-LRU movable pages */
	if ((flags & MF_SOFT_OFFLINE) && __PageMovable(page))
		return true;

	return PageLRU(page) || is_free_buddy_page(page);
}

static int __get_hwpoison_page(struct page *page, unsigned long flags)
{
	struct folio *folio = page_folio(page);
	int ret = 0;
	bool hugetlb = false;

	ret = get_hwpoison_hugetlb_folio(folio, &hugetlb, false);
	if (hugetlb)
		return ret;

	/*
	 * This check prevents from calling folio_try_get() for any
	 * unsupported type of folio in order to reduce the risk of unexpected
	 * races caused by taking a folio refcount.
	 */
	if (!HWPoisonHandlable(&folio->page, flags))
		return -EBUSY;

	if (folio_try_get(folio)) {
		if (folio == page_folio(page))
			return 1;

		pr_info("%#lx cannot catch tail\n", page_to_pfn(page));
		folio_put(folio);
	}

	return 0;
}

static int get_any_page(struct page *p, unsigned long flags)
{
	int ret = 0, pass = 0;
	bool count_increased = false;

	if (flags & MF_COUNT_INCREASED)
		count_increased = true;

try_again:
	if (!count_increased) {
		ret = __get_hwpoison_page(p, flags);
		if (!ret) {
			if (page_count(p)) {
				/* We raced with an allocation, retry. */
				if (pass++ < 3)
					goto try_again;
				ret = -EBUSY;
			} else if (!PageHuge(p) && !is_free_buddy_page(p)) {
				/* We raced with put_page, retry. */
				if (pass++ < 3)
					goto try_again;
				ret = -EIO;
			}
			goto out;
		} else if (ret == -EBUSY) {
			/*
			 * We raced with (possibly temporary) unhandlable
			 * page, retry.
			 */
			if (pass++ < 3) {
				shake_page(p);
				goto try_again;
			}
			ret = -EIO;
			goto out;
		}
	}

	if (PageHuge(p) || HWPoisonHandlable(p, flags)) {
		ret = 1;
	} else {
		/*
		 * A page we cannot handle. Check whether we can turn
		 * it into something we can handle.
		 */
		if (pass++ < 3) {
			put_page(p);
			shake_page(p);
			count_increased = false;
			goto try_again;
		}
		put_page(p);
		ret = -EIO;
	}
out:
	if (ret == -EIO)
		pr_err("%#lx: unhandlable page.\n", page_to_pfn(p));

	return ret;
}

static int __get_unpoison_page(struct page *page)
{
	struct folio *folio = page_folio(page);
	int ret = 0;
	bool hugetlb = false;

	ret = get_hwpoison_hugetlb_folio(folio, &hugetlb, true);
	if (hugetlb)
		return ret;

	/*
	 * PageHWPoisonTakenOff pages are not only marked as PG_hwpoison,
	 * but also isolated from buddy freelist, so need to identify the
	 * state and have to cancel both operations to unpoison.
	 */
	if (PageHWPoisonTakenOff(page))
		return -EHWPOISON;

	return get_page_unless_zero(page) ? 1 : 0;
}

/**
 * get_hwpoison_page() - Get refcount for memory error handling
 * @p:		Raw error page (hit by memory error)
 * @flags:	Flags controlling behavior of error handling
 *
 * get_hwpoison_page() takes a page refcount of an error page to handle memory
 * error on it, after checking that the error page is in a well-defined state
 * (defined as a page-type we can successfully handle the memory error on it,
 * such as LRU page and hugetlb page).
 *
 * Memory error handling could be triggered at any time on any type of page,
 * so it's prone to race with typical memory management lifecycle (like
 * allocation and free).  So to avoid such races, get_hwpoison_page() takes
 * extra care for the error page's state (as done in __get_hwpoison_page()),
 * and has some retry logic in get_any_page().
 *
 * When called from unpoison_memory(), the caller should already ensure that
 * the given page has PG_hwpoison. So it's never reused for other page
 * allocations, and __get_unpoison_page() never races with them.
 *
 * Return: 0 on failure,
 *         1 on success for in-use pages in a well-defined state,
 *         -EIO for pages on which we can not handle memory errors,
 *         -EBUSY when get_hwpoison_page() has raced with page lifecycle
 *         operations like allocation and free,
 *         -EHWPOISON when the page is hwpoisoned and taken off from buddy.
 */
static int get_hwpoison_page(struct page *p, unsigned long flags)
{
	int ret;

	zone_pcp_disable(page_zone(p));
	if (flags & MF_UNPOISON)
		ret = __get_unpoison_page(p);
	else
		ret = get_any_page(p, flags);
	zone_pcp_enable(page_zone(p));

	return ret;
}

/*
 * Do all that is necessary to remove user space mappings. Unmap
 * the pages and send SIGBUS to the processes if the data was dirty.
 */
static bool hwpoison_user_mappings(struct page *p, unsigned long pfn,
				  int flags, struct page *hpage)
{
	struct folio *folio = page_folio(hpage);
	enum ttu_flags ttu = TTU_IGNORE_MLOCK | TTU_SYNC | TTU_HWPOISON;
	struct address_space *mapping;
	LIST_HEAD(tokill);
	bool unmap_success;
	int forcekill;
	bool mlocked = PageMlocked(hpage);

	/*
	 * Here we are interested only in user-mapped pages, so skip any
	 * other types of pages.
	 */
	if (PageReserved(p) || PageSlab(p) || PageTable(p))
		return true;
	if (!(PageLRU(hpage) || PageHuge(p)))
		return true;

	/*
	 * This check implies we don't kill processes if their pages
	 * are in the swap cache early. Those are always late kills.
	 */
	if (!page_mapped(hpage))
		return true;

	if (PageSwapCache(p)) {
		pr_err("%#lx: keeping poisoned page in swap cache\n", pfn);
		ttu &= ~TTU_HWPOISON;
	}

	/*
	 * Propagate the dirty bit from PTEs to struct page first, because we
	 * need this to decide if we should kill or just drop the page.
	 * XXX: the dirty test could be racy: set_page_dirty() may not always
	 * be called inside page lock (it's recommended but not enforced).
	 */
	mapping = page_mapping(hpage);
	if (!(flags & MF_MUST_KILL) && !PageDirty(hpage) && mapping &&
	    mapping_can_writeback(mapping)) {
		if (page_mkclean(hpage)) {
			SetPageDirty(hpage);
		} else {
			ttu &= ~TTU_HWPOISON;
			pr_info("%#lx: corrupted page was clean: dropped without side effects\n",
				pfn);
		}
	}

	/*
	 * First collect all the processes that have the page
	 * mapped in dirty form.  This has to be done before try_to_unmap,
	 * because ttu takes the rmap data structures down.
	 */
	collect_procs(hpage, &tokill, flags & MF_ACTION_REQUIRED);

	if (PageHuge(hpage) && !PageAnon(hpage)) {
		/*
		 * For hugetlb pages in shared mappings, try_to_unmap
		 * could potentially call huge_pmd_unshare.  Because of
		 * this, take semaphore in write mode here and set
		 * TTU_RMAP_LOCKED to indicate we have taken the lock
		 * at this higher level.
		 */
		mapping = hugetlb_page_mapping_lock_write(hpage);
		if (mapping) {
			try_to_unmap(folio, ttu|TTU_RMAP_LOCKED);
			i_mmap_unlock_write(mapping);
		} else
			pr_info("%#lx: could not lock mapping for mapped huge page\n", pfn);
	} else {
		try_to_unmap(folio, ttu);
	}

	unmap_success = !page_mapped(hpage);
	if (!unmap_success)
		pr_err("%#lx: failed to unmap page (mapcount=%d)\n",
		       pfn, page_mapcount(hpage));

	/*
	 * try_to_unmap() might put mlocked page in lru cache, so call
	 * shake_page() again to ensure that it's flushed.
	 */
	if (mlocked)
		shake_page(hpage);

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
	forcekill = PageDirty(hpage) || (flags & MF_MUST_KILL) ||
		    !unmap_success;
	kill_procs(&tokill, forcekill, !unmap_success, pfn, flags);

	return unmap_success;
}

static int identify_page_state(unsigned long pfn, struct page *p,
				unsigned long page_flags)
{
	struct page_state *ps;

	/*
	 * The first check uses the current page flags which may not have any
	 * relevant information. The second check with the saved page flags is
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
	return page_action(ps, p, pfn);
}

static int try_to_split_thp_page(struct page *page)
{
	int ret;

	lock_page(page);
	ret = split_huge_page(page);
	unlock_page(page);

	if (unlikely(ret))
		put_page(page);

	return ret;
}

static void unmap_and_kill(struct list_head *to_kill, unsigned long pfn,
		struct address_space *mapping, pgoff_t index, int flags)
{
	struct to_kill *tk;
	unsigned long size = 0;

	list_for_each_entry(tk, to_kill, nd)
		if (tk->size_shift)
			size = max(size, 1UL << tk->size_shift);

	if (size) {
		/*
		 * Unmap the largest mapping to avoid breaking up device-dax
		 * mappings which are constant size. The actual size of the
		 * mapping being torn down is communicated in siginfo, see
		 * kill_proc()
		 */
		loff_t start = (index << PAGE_SHIFT) & ~(size - 1);

		unmap_mapping_range(mapping, start, size, 0);
	}

	kill_procs(to_kill, flags & MF_MUST_KILL, false, pfn, flags);
}

static int mf_generic_kill_procs(unsigned long long pfn, int flags,
		struct dev_pagemap *pgmap)
{
	struct page *page = pfn_to_page(pfn);
	LIST_HEAD(to_kill);
	dax_entry_t cookie;
	int rc = 0;

	/*
	 * Pages instantiated by device-dax (not filesystem-dax)
	 * may be compound pages.
	 */
	page = compound_head(page);

	/*
	 * Prevent the inode from being freed while we are interrogating
	 * the address_space, typically this would be handled by
	 * lock_page(), but dax pages do not use the page lock. This
	 * also prevents changes to the mapping of this pfn until
	 * poison signaling is complete.
	 */
	cookie = dax_lock_page(page);
	if (!cookie)
		return -EBUSY;

	if (hwpoison_filter(page)) {
		rc = -EOPNOTSUPP;
		goto unlock;
	}

	switch (pgmap->type) {
	case MEMORY_DEVICE_PRIVATE:
	case MEMORY_DEVICE_COHERENT:
		/*
		 * TODO: Handle device pages which may need coordination
		 * with device-side memory.
		 */
		rc = -ENXIO;
		goto unlock;
	default:
		break;
	}

	/*
	 * Use this flag as an indication that the dax page has been
	 * remapped UC to prevent speculative consumption of poison.
	 */
	SetPageHWPoison(page);

	/*
	 * Unlike System-RAM there is no possibility to swap in a
	 * different physical page at a given virtual address, so all
	 * userspace consumption of ZONE_DEVICE memory necessitates
	 * SIGBUS (i.e. MF_MUST_KILL)
	 */
	flags |= MF_ACTION_REQUIRED | MF_MUST_KILL;
	collect_procs(page, &to_kill, true);

	unmap_and_kill(&to_kill, pfn, page->mapping, page->index, flags);
unlock:
	dax_unlock_page(page, cookie);
	return rc;
}

#ifdef CONFIG_FS_DAX
/**
 * mf_dax_kill_procs - Collect and kill processes who are using this file range
 * @mapping:	address_space of the file in use
 * @index:	start pgoff of the range within the file
 * @count:	length of the range, in unit of PAGE_SIZE
 * @mf_flags:	memory failure flags
 */
int mf_dax_kill_procs(struct address_space *mapping, pgoff_t index,
		unsigned long count, int mf_flags)
{
	LIST_HEAD(to_kill);
	dax_entry_t cookie;
	struct page *page;
	size_t end = index + count;

	mf_flags |= MF_ACTION_REQUIRED | MF_MUST_KILL;

	for (; index < end; index++) {
		page = NULL;
		cookie = dax_lock_mapping_entry(mapping, index, &page);
		if (!cookie)
			return -EBUSY;
		if (!page)
			goto unlock;

		SetPageHWPoison(page);

		collect_procs_fsdax(page, mapping, index, &to_kill);
		unmap_and_kill(&to_kill, page_to_pfn(page), mapping,
				index, mf_flags);
unlock:
		dax_unlock_mapping_entry(mapping, index, cookie);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(mf_dax_kill_procs);
#endif /* CONFIG_FS_DAX */

#ifdef CONFIG_HUGETLB_PAGE
/*
 * Struct raw_hwp_page represents information about "raw error page",
 * constructing singly linked list from ->_hugetlb_hwpoison field of folio.
 */
struct raw_hwp_page {
	struct llist_node node;
	struct page *page;
};

static inline struct llist_head *raw_hwp_list_head(struct folio *folio)
{
	return (struct llist_head *)&folio->_hugetlb_hwpoison;
}

static unsigned long __folio_free_raw_hwp(struct folio *folio, bool move_flag)
{
	struct llist_head *head;
	struct llist_node *t, *tnode;
	unsigned long count = 0;

	head = raw_hwp_list_head(folio);
	llist_for_each_safe(tnode, t, head->first) {
		struct raw_hwp_page *p = container_of(tnode, struct raw_hwp_page, node);

		if (move_flag)
			SetPageHWPoison(p->page);
		else
			num_poisoned_pages_sub(page_to_pfn(p->page), 1);
		kfree(p);
		count++;
	}
	llist_del_all(head);
	return count;
}

static int folio_set_hugetlb_hwpoison(struct folio *folio, struct page *page)
{
	struct llist_head *head;
	struct raw_hwp_page *raw_hwp;
	struct llist_node *t, *tnode;
	int ret = folio_test_set_hwpoison(folio) ? -EHWPOISON : 0;

	/*
	 * Once the hwpoison hugepage has lost reliable raw error info,
	 * there is little meaning to keep additional error info precisely,
	 * so skip to add additional raw error info.
	 */
	if (folio_test_hugetlb_raw_hwp_unreliable(folio))
		return -EHWPOISON;
	head = raw_hwp_list_head(folio);
	llist_for_each_safe(tnode, t, head->first) {
		struct raw_hwp_page *p = container_of(tnode, struct raw_hwp_page, node);

		if (p->page == page)
			return -EHWPOISON;
	}

	raw_hwp = kmalloc(sizeof(struct raw_hwp_page), GFP_ATOMIC);
	if (raw_hwp) {
		raw_hwp->page = page;
		llist_add(&raw_hwp->node, head);
		/* the first error event will be counted in action_result(). */
		if (ret)
			num_poisoned_pages_inc(page_to_pfn(page));
	} else {
		/*
		 * Failed to save raw error info.  We no longer trace all
		 * hwpoisoned subpages, and we need refuse to free/dissolve
		 * this hwpoisoned hugepage.
		 */
		folio_set_hugetlb_raw_hwp_unreliable(folio);
		/*
		 * Once hugetlb_raw_hwp_unreliable is set, raw_hwp_page is not
		 * used any more, so free it.
		 */
		__folio_free_raw_hwp(folio, false);
	}
	return ret;
}

static unsigned long folio_free_raw_hwp(struct folio *folio, bool move_flag)
{
	/*
	 * hugetlb_vmemmap_optimized hugepages can't be freed because struct
	 * pages for tail pages are required but they don't exist.
	 */
	if (move_flag && folio_test_hugetlb_vmemmap_optimized(folio))
		return 0;

	/*
	 * hugetlb_raw_hwp_unreliable hugepages shouldn't be unpoisoned by
	 * definition.
	 */
	if (folio_test_hugetlb_raw_hwp_unreliable(folio))
		return 0;

	return __folio_free_raw_hwp(folio, move_flag);
}

void folio_clear_hugetlb_hwpoison(struct folio *folio)
{
	if (folio_test_hugetlb_raw_hwp_unreliable(folio))
		return;
	folio_clear_hwpoison(folio);
	folio_free_raw_hwp(folio, true);
}

/*
 * Called from hugetlb code with hugetlb_lock held.
 *
 * Return values:
 *   0             - free hugepage
 *   1             - in-use hugepage
 *   2             - not a hugepage
 *   -EBUSY        - the hugepage is busy (try to retry)
 *   -EHWPOISON    - the hugepage is already hwpoisoned
 */
int __get_huge_page_for_hwpoison(unsigned long pfn, int flags,
				 bool *migratable_cleared)
{
	struct page *page = pfn_to_page(pfn);
	struct folio *folio = page_folio(page);
	int ret = 2;	/* fallback to normal page handling */
	bool count_increased = false;

	if (!folio_test_hugetlb(folio))
		goto out;

	if (flags & MF_COUNT_INCREASED) {
		ret = 1;
		count_increased = true;
	} else if (folio_test_hugetlb_freed(folio)) {
		ret = 0;
	} else if (folio_test_hugetlb_migratable(folio)) {
		ret = folio_try_get(folio);
		if (ret)
			count_increased = true;
	} else {
		ret = -EBUSY;
		if (!(flags & MF_NO_RETRY))
			goto out;
	}

	if (folio_set_hugetlb_hwpoison(folio, page)) {
		ret = -EHWPOISON;
		goto out;
	}

	/*
	 * Clearing hugetlb_migratable for hwpoisoned hugepages to prevent them
	 * from being migrated by memory hotremove.
	 */
	if (count_increased && folio_test_hugetlb_migratable(folio)) {
		folio_clear_hugetlb_migratable(folio);
		*migratable_cleared = true;
	}

	return ret;
out:
	if (count_increased)
		folio_put(folio);
	return ret;
}

/*
 * Taking refcount of hugetlb pages needs extra care about race conditions
 * with basic operations like hugepage allocation/free/demotion.
 * So some of prechecks for hwpoison (pinning, and testing/setting
 * PageHWPoison) should be done in single hugetlb_lock range.
 */
static int try_memory_failure_hugetlb(unsigned long pfn, int flags, int *hugetlb)
{
	int res;
	struct page *p = pfn_to_page(pfn);
	struct folio *folio;
	unsigned long page_flags;
	bool migratable_cleared = false;

	*hugetlb = 1;
retry:
	res = get_huge_page_for_hwpoison(pfn, flags, &migratable_cleared);
	if (res == 2) { /* fallback to normal page handling */
		*hugetlb = 0;
		return 0;
	} else if (res == -EHWPOISON) {
		pr_err("%#lx: already hardware poisoned\n", pfn);
		if (flags & MF_ACTION_REQUIRED) {
			folio = page_folio(p);
			res = kill_accessing_process(current, folio_pfn(folio), flags);
		}
		return res;
	} else if (res == -EBUSY) {
		if (!(flags & MF_NO_RETRY)) {
			flags |= MF_NO_RETRY;
			goto retry;
		}
		return action_result(pfn, MF_MSG_UNKNOWN, MF_IGNORED);
	}

	folio = page_folio(p);
	folio_lock(folio);

	if (hwpoison_filter(p)) {
		folio_clear_hugetlb_hwpoison(folio);
		if (migratable_cleared)
			folio_set_hugetlb_migratable(folio);
		folio_unlock(folio);
		if (res == 1)
			folio_put(folio);
		return -EOPNOTSUPP;
	}

	/*
	 * Handling free hugepage.  The possible race with hugepage allocation
	 * or demotion can be prevented by PageHWPoison flag.
	 */
	if (res == 0) {
		folio_unlock(folio);
		if (__page_handle_poison(p) >= 0) {
			page_ref_inc(p);
			res = MF_RECOVERED;
		} else {
			res = MF_FAILED;
		}
		return action_result(pfn, MF_MSG_FREE_HUGE, res);
	}

	page_flags = folio->flags;

	if (!hwpoison_user_mappings(p, pfn, flags, &folio->page)) {
		folio_unlock(folio);
		return action_result(pfn, MF_MSG_UNMAP_FAILED, MF_IGNORED);
	}

	return identify_page_state(pfn, p, page_flags);
}

#else
static inline int try_memory_failure_hugetlb(unsigned long pfn, int flags, int *hugetlb)
{
	return 0;
}

static inline unsigned long folio_free_raw_hwp(struct folio *folio, bool flag)
{
	return 0;
}
#endif	/* CONFIG_HUGETLB_PAGE */

/* Drop the extra refcount in case we come from madvise() */
static void put_ref_page(unsigned long pfn, int flags)
{
	struct page *page;

	if (!(flags & MF_COUNT_INCREASED))
		return;

	page = pfn_to_page(pfn);
	if (page)
		put_page(page);
}

static int memory_failure_dev_pagemap(unsigned long pfn, int flags,
		struct dev_pagemap *pgmap)
{
	int rc = -ENXIO;

	put_ref_page(pfn, flags);

	/* device metadata space is not recoverable */
	if (!pgmap_pfn_valid(pgmap, pfn))
		goto out;

	/*
	 * Call driver's implementation to handle the memory failure, otherwise
	 * fall back to generic handler.
	 */
	if (pgmap_has_memory_failure(pgmap)) {
		rc = pgmap->ops->memory_failure(pgmap, pfn, 1, flags);
		/*
		 * Fall back to generic handler too if operation is not
		 * supported inside the driver/device/filesystem.
		 */
		if (rc != -EOPNOTSUPP)
			goto out;
	}

	rc = mf_generic_kill_procs(pfn, flags, pgmap);
out:
	/* drop pgmap ref acquired in caller */
	put_dev_pagemap(pgmap);
	action_result(pfn, MF_MSG_DAX, rc ? MF_FAILED : MF_RECOVERED);
	return rc;
}

static DEFINE_MUTEX(mf_mutex);

/**
 * memory_failure - Handle memory failure of a page.
 * @pfn: Page Number of the corrupted page
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
 *
 * Return: 0 for successfully handled the memory error,
 *         -EOPNOTSUPP for hwpoison_filter() filtered the error event,
 *         < 0(except -EOPNOTSUPP) on failure.
 */
int memory_failure(unsigned long pfn, int flags)
{
	struct page *p;
	struct page *hpage;
	struct dev_pagemap *pgmap;
	int res = 0;
	unsigned long page_flags;
	bool retry = true;
	int hugetlb = 0;

	if (!sysctl_memory_failure_recovery)
		panic("Memory failure on page %lx", pfn);

	mutex_lock(&mf_mutex);

	if (!(flags & MF_SW_SIMULATED))
		hw_memory_failure = true;

	p = pfn_to_online_page(pfn);
	if (!p) {
		res = arch_memory_failure(pfn, flags);
		if (res == 0)
			goto unlock_mutex;

		if (pfn_valid(pfn)) {
			pgmap = get_dev_pagemap(pfn, NULL);
			if (pgmap) {
				res = memory_failure_dev_pagemap(pfn, flags,
								 pgmap);
				goto unlock_mutex;
			}
		}
		pr_err("%#lx: memory outside kernel control\n", pfn);
		res = -ENXIO;
		goto unlock_mutex;
	}

try_again:
	res = try_memory_failure_hugetlb(pfn, flags, &hugetlb);
	if (hugetlb)
		goto unlock_mutex;

	if (TestSetPageHWPoison(p)) {
		pr_err("%#lx: already hardware poisoned\n", pfn);
		res = -EHWPOISON;
		if (flags & MF_ACTION_REQUIRED)
			res = kill_accessing_process(current, pfn, flags);
		if (flags & MF_COUNT_INCREASED)
			put_page(p);
		goto unlock_mutex;
	}

	hpage = compound_head(p);

	/*
	 * We need/can do nothing about count=0 pages.
	 * 1) it's a free page, and therefore in safe hand:
	 *    check_new_page() will be the gate keeper.
	 * 2) it's part of a non-compound high order page.
	 *    Implies some kernel user: cannot stop them from
	 *    R/W the page; let's pray that the page has been
	 *    used and will be freed some time later.
	 * In fact it's dangerous to directly bump up page count from 0,
	 * that may make page_ref_freeze()/page_ref_unfreeze() mismatch.
	 */
	if (!(flags & MF_COUNT_INCREASED)) {
		res = get_hwpoison_page(p, flags);
		if (!res) {
			if (is_free_buddy_page(p)) {
				if (take_page_off_buddy(p)) {
					page_ref_inc(p);
					res = MF_RECOVERED;
				} else {
					/* We lost the race, try again */
					if (retry) {
						ClearPageHWPoison(p);
						retry = false;
						goto try_again;
					}
					res = MF_FAILED;
				}
				res = action_result(pfn, MF_MSG_BUDDY, res);
			} else {
				res = action_result(pfn, MF_MSG_KERNEL_HIGH_ORDER, MF_IGNORED);
			}
			goto unlock_mutex;
		} else if (res < 0) {
			res = action_result(pfn, MF_MSG_UNKNOWN, MF_IGNORED);
			goto unlock_mutex;
		}
	}

	if (PageTransHuge(hpage)) {
		/*
		 * The flag must be set after the refcount is bumped
		 * otherwise it may race with THP split.
		 * And the flag can't be set in get_hwpoison_page() since
		 * it is called by soft offline too and it is just called
		 * for !MF_COUNT_INCREASE.  So here seems to be the best
		 * place.
		 *
		 * Don't need care about the above error handling paths for
		 * get_hwpoison_page() since they handle either free page
		 * or unhandlable page.  The refcount is bumped iff the
		 * page is a valid handlable page.
		 */
		SetPageHasHWPoisoned(hpage);
		if (try_to_split_thp_page(p) < 0) {
			res = action_result(pfn, MF_MSG_UNSPLIT_THP, MF_IGNORED);
			goto unlock_mutex;
		}
		VM_BUG_ON_PAGE(!page_count(p), p);
	}

	/*
	 * We ignore non-LRU pages for good reasons.
	 * - PG_locked is only well defined for LRU pages and a few others
	 * - to avoid races with __SetPageLocked()
	 * - to avoid races with __SetPageSlab*() (and more non-atomic ops)
	 * The check (unnecessarily) ignores LRU pages being isolated and
	 * walked by the page reclaim code, however that's not a big loss.
	 */
	shake_page(p);

	lock_page(p);

	/*
	 * We're only intended to deal with the non-Compound page here.
	 * However, the page could have changed compound pages due to
	 * race window. If this happens, we could try again to hopefully
	 * handle the page next round.
	 */
	if (PageCompound(p)) {
		if (retry) {
			ClearPageHWPoison(p);
			unlock_page(p);
			put_page(p);
			flags &= ~MF_COUNT_INCREASED;
			retry = false;
			goto try_again;
		}
		res = action_result(pfn, MF_MSG_DIFFERENT_COMPOUND, MF_IGNORED);
		goto unlock_page;
	}

	/*
	 * We use page flags to determine what action should be taken, but
	 * the flags can be modified by the error containment action.  One
	 * example is an mlocked page, where PG_mlocked is cleared by
	 * page_remove_rmap() in try_to_unmap_one(). So to determine page status
	 * correctly, we save a copy of the page flags at this time.
	 */
	page_flags = p->flags;

	if (hwpoison_filter(p)) {
		ClearPageHWPoison(p);
		unlock_page(p);
		put_page(p);
		res = -EOPNOTSUPP;
		goto unlock_mutex;
	}

	/*
	 * __munlock_folio() may clear a writeback page's LRU flag without
	 * page_lock. We need wait writeback completion for this page or it
	 * may trigger vfs BUG while evict inode.
	 */
	if (!PageLRU(p) && !PageWriteback(p))
		goto identify_page_state;

	/*
	 * It's very difficult to mess with pages currently under IO
	 * and in many cases impossible, so we just avoid it here.
	 */
	wait_on_page_writeback(p);

	/*
	 * Now take care of user space mappings.
	 * Abort on fail: __filemap_remove_folio() assumes unmapped page.
	 */
	if (!hwpoison_user_mappings(p, pfn, flags, p)) {
		res = action_result(pfn, MF_MSG_UNMAP_FAILED, MF_IGNORED);
		goto unlock_page;
	}

	/*
	 * Torn down by someone else?
	 */
	if (PageLRU(p) && !PageSwapCache(p) && p->mapping == NULL) {
		res = action_result(pfn, MF_MSG_TRUNCATED_LRU, MF_IGNORED);
		goto unlock_page;
	}

identify_page_state:
	res = identify_page_state(pfn, p, page_flags);
	mutex_unlock(&mf_mutex);
	return res;
unlock_page:
	unlock_page(p);
unlock_mutex:
	mutex_unlock(&mf_mutex);
	return res;
}
EXPORT_SYMBOL_GPL(memory_failure);

#define MEMORY_FAILURE_FIFO_ORDER	4
#define MEMORY_FAILURE_FIFO_SIZE	(1 << MEMORY_FAILURE_FIFO_ORDER)

struct memory_failure_entry {
	unsigned long pfn;
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
void memory_failure_queue(unsigned long pfn, int flags)
{
	struct memory_failure_cpu *mf_cpu;
	unsigned long proc_flags;
	struct memory_failure_entry entry = {
		.pfn =		pfn,
		.flags =	flags,
	};

	mf_cpu = &get_cpu_var(memory_failure_cpu);
	spin_lock_irqsave(&mf_cpu->lock, proc_flags);
	if (kfifo_put(&mf_cpu->fifo, entry))
		schedule_work_on(smp_processor_id(), &mf_cpu->work);
	else
		pr_err("buffer overflow when queuing memory failure at %#lx\n",
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

	mf_cpu = container_of(work, struct memory_failure_cpu, work);
	for (;;) {
		spin_lock_irqsave(&mf_cpu->lock, proc_flags);
		gotten = kfifo_get(&mf_cpu->fifo, &entry);
		spin_unlock_irqrestore(&mf_cpu->lock, proc_flags);
		if (!gotten)
			break;
		if (entry.flags & MF_SOFT_OFFLINE)
			soft_offline_page(entry.pfn, entry.flags);
		else
			memory_failure(entry.pfn, entry.flags);
	}
}

/*
 * Process memory_failure work queued on the specified CPU.
 * Used to avoid return-to-userspace racing with the memory_failure workqueue.
 */
void memory_failure_queue_kick(int cpu)
{
	struct memory_failure_cpu *mf_cpu;

	mf_cpu = &per_cpu(memory_failure_cpu, cpu);
	cancel_work_sync(&mf_cpu->work);
	memory_failure_work_func(&mf_cpu->work);
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

	register_sysctl_init("vm", memory_failure_table);

	return 0;
}
core_initcall(memory_failure_init);

#undef pr_fmt
#define pr_fmt(fmt)	"" fmt
#define unpoison_pr_info(fmt, pfn, rs)			\
({							\
	if (__ratelimit(rs))				\
		pr_info(fmt, pfn);			\
})

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
	struct folio *folio;
	struct page *p;
	int ret = -EBUSY, ghp;
	unsigned long count = 1;
	bool huge = false;
	static DEFINE_RATELIMIT_STATE(unpoison_rs, DEFAULT_RATELIMIT_INTERVAL,
					DEFAULT_RATELIMIT_BURST);

	if (!pfn_valid(pfn))
		return -ENXIO;

	p = pfn_to_page(pfn);
	folio = page_folio(p);

	mutex_lock(&mf_mutex);

	if (hw_memory_failure) {
		unpoison_pr_info("Unpoison: Disabled after HW memory failure %#lx\n",
				 pfn, &unpoison_rs);
		ret = -EOPNOTSUPP;
		goto unlock_mutex;
	}

	if (!PageHWPoison(p)) {
		unpoison_pr_info("Unpoison: Page was already unpoisoned %#lx\n",
				 pfn, &unpoison_rs);
		goto unlock_mutex;
	}

	if (folio_ref_count(folio) > 1) {
		unpoison_pr_info("Unpoison: Someone grabs the hwpoison page %#lx\n",
				 pfn, &unpoison_rs);
		goto unlock_mutex;
	}

	if (folio_test_slab(folio) || PageTable(&folio->page) || folio_test_reserved(folio))
		goto unlock_mutex;

	/*
	 * Note that folio->_mapcount is overloaded in SLAB, so the simple test
	 * in folio_mapped() has to be done after folio_test_slab() is checked.
	 */
	if (folio_mapped(folio)) {
		unpoison_pr_info("Unpoison: Someone maps the hwpoison page %#lx\n",
				 pfn, &unpoison_rs);
		goto unlock_mutex;
	}

	if (folio_mapping(folio)) {
		unpoison_pr_info("Unpoison: the hwpoison page has non-NULL mapping %#lx\n",
				 pfn, &unpoison_rs);
		goto unlock_mutex;
	}

	ghp = get_hwpoison_page(p, MF_UNPOISON);
	if (!ghp) {
		if (PageHuge(p)) {
			huge = true;
			count = folio_free_raw_hwp(folio, false);
			if (count == 0)
				goto unlock_mutex;
		}
		ret = folio_test_clear_hwpoison(folio) ? 0 : -EBUSY;
	} else if (ghp < 0) {
		if (ghp == -EHWPOISON) {
			ret = put_page_back_buddy(p) ? 0 : -EBUSY;
		} else {
			ret = ghp;
			unpoison_pr_info("Unpoison: failed to grab page %#lx\n",
					 pfn, &unpoison_rs);
		}
	} else {
		if (PageHuge(p)) {
			huge = true;
			count = folio_free_raw_hwp(folio, false);
			if (count == 0) {
				folio_put(folio);
				goto unlock_mutex;
			}
		}

		folio_put(folio);
		if (TestClearPageHWPoison(p)) {
			folio_put(folio);
			ret = 0;
		}
	}

unlock_mutex:
	mutex_unlock(&mf_mutex);
	if (!ret) {
		if (!huge)
			num_poisoned_pages_sub(pfn, 1);
		unpoison_pr_info("Unpoison: Software-unpoisoned page %#lx\n",
				 page_to_pfn(p), &unpoison_rs);
	}
	return ret;
}
EXPORT_SYMBOL(unpoison_memory);

static bool isolate_page(struct page *page, struct list_head *pagelist)
{
	bool isolated = false;

	if (PageHuge(page)) {
		isolated = isolate_hugetlb(page_folio(page), pagelist);
	} else {
		bool lru = !__PageMovable(page);

		if (lru)
			isolated = isolate_lru_page(page);
		else
			isolated = isolate_movable_page(page,
							ISOLATE_UNEVICTABLE);

		if (isolated) {
			list_add(&page->lru, pagelist);
			if (lru)
				inc_node_page_state(page, NR_ISOLATED_ANON +
						    page_is_file_lru(page));
		}
	}

	/*
	 * If we succeed to isolate the page, we grabbed another refcount on
	 * the page, so we can safely drop the one we got from get_any_pages().
	 * If we failed to isolate the page, it means that we cannot go further
	 * and we will return an error, so drop the reference we got from
	 * get_any_pages() as well.
	 */
	put_page(page);
	return isolated;
}

/*
 * soft_offline_in_use_page handles hugetlb-pages and non-hugetlb pages.
 * If the page is a non-dirty unmapped page-cache page, it simply invalidates.
 * If the page is mapped, it migrates the contents over.
 */
static int soft_offline_in_use_page(struct page *page)
{
	long ret = 0;
	unsigned long pfn = page_to_pfn(page);
	struct page *hpage = compound_head(page);
	char const *msg_page[] = {"page", "hugepage"};
	bool huge = PageHuge(page);
	LIST_HEAD(pagelist);
	struct migration_target_control mtc = {
		.nid = NUMA_NO_NODE,
		.gfp_mask = GFP_USER | __GFP_MOVABLE | __GFP_RETRY_MAYFAIL,
	};

	if (!huge && PageTransHuge(hpage)) {
		if (try_to_split_thp_page(page)) {
			pr_info("soft offline: %#lx: thp split failed\n", pfn);
			return -EBUSY;
		}
		hpage = page;
	}

	lock_page(page);
	if (!PageHuge(page))
		wait_on_page_writeback(page);
	if (PageHWPoison(page)) {
		unlock_page(page);
		put_page(page);
		pr_info("soft offline: %#lx page already poisoned\n", pfn);
		return 0;
	}

	if (!PageHuge(page) && PageLRU(page) && !PageSwapCache(page))
		/*
		 * Try to invalidate first. This should work for
		 * non dirty unmapped page cache pages.
		 */
		ret = invalidate_inode_page(page);
	unlock_page(page);

	if (ret) {
		pr_info("soft_offline: %#lx: invalidated\n", pfn);
		page_handle_poison(page, false, true);
		return 0;
	}

	if (isolate_page(hpage, &pagelist)) {
		ret = migrate_pages(&pagelist, alloc_migration_target, NULL,
			(unsigned long)&mtc, MIGRATE_SYNC, MR_MEMORY_FAILURE, NULL);
		if (!ret) {
			bool release = !huge;

			if (!page_handle_poison(page, huge, release))
				ret = -EBUSY;
		} else {
			if (!list_empty(&pagelist))
				putback_movable_pages(&pagelist);

			pr_info("soft offline: %#lx: %s migration failed %ld, type %pGp\n",
				pfn, msg_page[huge], ret, &page->flags);
			if (ret > 0)
				ret = -EBUSY;
		}
	} else {
		pr_info("soft offline: %#lx: %s isolation failed, page count %d, type %pGp\n",
			pfn, msg_page[huge], page_count(page), &page->flags);
		ret = -EBUSY;
	}
	return ret;
}

/**
 * soft_offline_page - Soft offline a page.
 * @pfn: pfn to soft-offline
 * @flags: flags. Same as memory_failure().
 *
 * Returns 0 on success
 *         -EOPNOTSUPP for hwpoison_filter() filtered the error event
 *         < 0 otherwise negated errno.
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
int soft_offline_page(unsigned long pfn, int flags)
{
	int ret;
	bool try_again = true;
	struct page *page;

	if (!pfn_valid(pfn)) {
		WARN_ON_ONCE(flags & MF_COUNT_INCREASED);
		return -ENXIO;
	}

	/* Only online pages can be soft-offlined (esp., not ZONE_DEVICE). */
	page = pfn_to_online_page(pfn);
	if (!page) {
		put_ref_page(pfn, flags);
		return -EIO;
	}

	mutex_lock(&mf_mutex);

	if (PageHWPoison(page)) {
		pr_info("%s: %#lx page already poisoned\n", __func__, pfn);
		put_ref_page(pfn, flags);
		mutex_unlock(&mf_mutex);
		return 0;
	}

retry:
	get_online_mems();
	ret = get_hwpoison_page(page, flags | MF_SOFT_OFFLINE);
	put_online_mems();

	if (hwpoison_filter(page)) {
		if (ret > 0)
			put_page(page);

		mutex_unlock(&mf_mutex);
		return -EOPNOTSUPP;
	}

	if (ret > 0) {
		ret = soft_offline_in_use_page(page);
	} else if (ret == 0) {
		if (!page_handle_poison(page, true, false)) {
			if (try_again) {
				try_again = false;
				flags &= ~MF_COUNT_INCREASED;
				goto retry;
			}
			ret = -EBUSY;
		}
	}

	mutex_unlock(&mf_mutex);

	return ret;
}
