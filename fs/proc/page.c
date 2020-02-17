// SPDX-License-Identifier: GPL-2.0
#include <linux/bootmem.h>
#include <linux/compiler.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ksm.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/huge_mm.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/hugetlb.h>
#include <linux/memcontrol.h>
#include <linux/mmu_notifier.h>
#include <linux/page_idle.h>
#include <linux/kernel-page-flags.h>
#include <linux/uaccess.h>
#include "internal.h"

#define KPMSIZE sizeof(u64)
#define KPMMASK (KPMSIZE - 1)
#define KPMBITS (KPMSIZE * BITS_PER_BYTE)

/* /proc/kpagecount - an array exposing page counts
 *
 * Each entry is a u64 representing the corresponding
 * physical page count.
 */
static ssize_t kpagecount_read(struct file *file, char __user *buf,
			     size_t count, loff_t *ppos)
{
	u64 __user *out = (u64 __user *)buf;
	struct page *ppage;
	unsigned long src = *ppos;
	unsigned long pfn;
	ssize_t ret = 0;
	u64 pcount;

	pfn = src / KPMSIZE;
	count = min_t(size_t, count, (max_pfn * KPMSIZE) - src);
	if (src & KPMMASK || count & KPMMASK)
		return -EINVAL;

	while (count > 0) {
		/*
		 * TODO: ZONE_DEVICE support requires to identify
		 * memmaps that were actually initialized.
		 */
		ppage = pfn_to_online_page(pfn);

		if (!ppage || PageSlab(ppage))
			pcount = 0;
		else
			pcount = page_mapcount(ppage);

		if (put_user(pcount, out)) {
			ret = -EFAULT;
			break;
		}

		pfn++;
		out++;
		count -= KPMSIZE;

		cond_resched();
	}

	*ppos += (char __user *)out - buf;
	if (!ret)
		ret = (char __user *)out - buf;
	return ret;
}

static const struct file_operations proc_kpagecount_operations = {
	.llseek = mem_lseek,
	.read = kpagecount_read,
};

/* /proc/kpageflags - an array exposing page flags
 *
 * Each entry is a u64 representing the corresponding
 * physical page flags.
 */

static inline u64 kpf_copy_bit(u64 kflags, int ubit, int kbit)
{
	return ((kflags >> kbit) & 1) << ubit;
}

u64 stable_page_flags(struct page *page)
{
	u64 k;
	u64 u;

	/*
	 * pseudo flag: KPF_NOPAGE
	 * it differentiates a memory hole from a page with no flags
	 */
	if (!page)
		return 1 << KPF_NOPAGE;

	k = page->flags;
	u = 0;

	/*
	 * pseudo flags for the well known (anonymous) memory mapped pages
	 *
	 * Note that page->_mapcount is overloaded in SLOB/SLUB/SLQB, so the
	 * simple test in page_mapped() is not enough.
	 */
	if (!PageSlab(page) && page_mapped(page))
		u |= 1 << KPF_MMAP;
	if (PageAnon(page))
		u |= 1 << KPF_ANON;
	if (PageKsm(page))
		u |= 1 << KPF_KSM;

	/*
	 * compound pages: export both head/tail info
	 * they together define a compound page's start/end pos and order
	 */
	if (PageHead(page))
		u |= 1 << KPF_COMPOUND_HEAD;
	if (PageTail(page))
		u |= 1 << KPF_COMPOUND_TAIL;
	if (PageHuge(page))
		u |= 1 << KPF_HUGE;
	/*
	 * PageTransCompound can be true for non-huge compound pages (slab
	 * pages or pages allocated by drivers with __GFP_COMP) because it
	 * just checks PG_head/PG_tail, so we need to check PageLRU/PageAnon
	 * to make sure a given page is a thp, not a non-huge compound page.
	 */
	else if (PageTransCompound(page)) {
		struct page *head = compound_head(page);

		if (PageLRU(head) || PageAnon(head))
			u |= 1 << KPF_THP;
		else if (is_huge_zero_page(head)) {
			u |= 1 << KPF_ZERO_PAGE;
			u |= 1 << KPF_THP;
		}
	} else if (is_zero_pfn(page_to_pfn(page)))
		u |= 1 << KPF_ZERO_PAGE;


	/*
	 * Caveats on high order pages: page->_refcount will only be set
	 * -1 on the head page; SLUB/SLQB do the same for PG_slab;
	 * SLOB won't set PG_slab at all on compound pages.
	 */
	if (PageBuddy(page))
		u |= 1 << KPF_BUDDY;
	else if (page_count(page) == 0 && is_free_buddy_page(page))
		u |= 1 << KPF_BUDDY;

	if (PageBalloon(page))
		u |= 1 << KPF_BALLOON;
	if (PageTable(page))
		u |= 1 << KPF_PGTABLE;

	if (page_is_idle(page))
		u |= 1 << KPF_IDLE;

	u |= kpf_copy_bit(k, KPF_LOCKED,	PG_locked);

	u |= kpf_copy_bit(k, KPF_SLAB,		PG_slab);
	if (PageTail(page) && PageSlab(compound_head(page)))
		u |= 1 << KPF_SLAB;

	u |= kpf_copy_bit(k, KPF_ERROR,		PG_error);
	u |= kpf_copy_bit(k, KPF_DIRTY,		PG_dirty);
	u |= kpf_copy_bit(k, KPF_UPTODATE,	PG_uptodate);
	u |= kpf_copy_bit(k, KPF_WRITEBACK,	PG_writeback);

	u |= kpf_copy_bit(k, KPF_LRU,		PG_lru);
	u |= kpf_copy_bit(k, KPF_REFERENCED,	PG_referenced);
	u |= kpf_copy_bit(k, KPF_ACTIVE,	PG_active);
	u |= kpf_copy_bit(k, KPF_RECLAIM,	PG_reclaim);

	if (PageSwapCache(page))
		u |= 1 << KPF_SWAPCACHE;
	u |= kpf_copy_bit(k, KPF_SWAPBACKED,	PG_swapbacked);

	u |= kpf_copy_bit(k, KPF_UNEVICTABLE,	PG_unevictable);
	u |= kpf_copy_bit(k, KPF_MLOCKED,	PG_mlocked);

#ifdef CONFIG_MEMORY_FAILURE
	u |= kpf_copy_bit(k, KPF_HWPOISON,	PG_hwpoison);
#endif

#ifdef CONFIG_ARCH_USES_PG_UNCACHED
	u |= kpf_copy_bit(k, KPF_UNCACHED,	PG_uncached);
#endif

	u |= kpf_copy_bit(k, KPF_RESERVED,	PG_reserved);
	u |= kpf_copy_bit(k, KPF_MAPPEDTODISK,	PG_mappedtodisk);
	u |= kpf_copy_bit(k, KPF_PRIVATE,	PG_private);
	u |= kpf_copy_bit(k, KPF_PRIVATE_2,	PG_private_2);
	u |= kpf_copy_bit(k, KPF_OWNER_PRIVATE,	PG_owner_priv_1);
	u |= kpf_copy_bit(k, KPF_ARCH,		PG_arch_1);

	return u;
};

static ssize_t kpageflags_read(struct file *file, char __user *buf,
			     size_t count, loff_t *ppos)
{
	u64 __user *out = (u64 __user *)buf;
	struct page *ppage;
	unsigned long src = *ppos;
	unsigned long pfn;
	ssize_t ret = 0;

	pfn = src / KPMSIZE;
	count = min_t(unsigned long, count, (max_pfn * KPMSIZE) - src);
	if (src & KPMMASK || count & KPMMASK)
		return -EINVAL;

	while (count > 0) {
		/*
		 * TODO: ZONE_DEVICE support requires to identify
		 * memmaps that were actually initialized.
		 */
		ppage = pfn_to_online_page(pfn);

		if (put_user(stable_page_flags(ppage), out)) {
			ret = -EFAULT;
			break;
		}

		pfn++;
		out++;
		count -= KPMSIZE;

		cond_resched();
	}

	*ppos += (char __user *)out - buf;
	if (!ret)
		ret = (char __user *)out - buf;
	return ret;
}

static const struct file_operations proc_kpageflags_operations = {
	.llseek = mem_lseek,
	.read = kpageflags_read,
};

#ifdef CONFIG_MEMCG
static ssize_t kpagecgroup_read(struct file *file, char __user *buf,
				size_t count, loff_t *ppos)
{
	u64 __user *out = (u64 __user *)buf;
	struct page *ppage;
	unsigned long src = *ppos;
	unsigned long pfn;
	ssize_t ret = 0;
	u64 ino;

	pfn = src / KPMSIZE;
	count = min_t(unsigned long, count, (max_pfn * KPMSIZE) - src);
	if (src & KPMMASK || count & KPMMASK)
		return -EINVAL;

	while (count > 0) {
		/*
		 * TODO: ZONE_DEVICE support requires to identify
		 * memmaps that were actually initialized.
		 */
		ppage = pfn_to_online_page(pfn);

		if (ppage)
			ino = page_cgroup_ino(ppage);
		else
			ino = 0;

		if (put_user(ino, out)) {
			ret = -EFAULT;
			break;
		}

		pfn++;
		out++;
		count -= KPMSIZE;

		cond_resched();
	}

	*ppos += (char __user *)out - buf;
	if (!ret)
		ret = (char __user *)out - buf;
	return ret;
}

static const struct file_operations proc_kpagecgroup_operations = {
	.llseek = mem_lseek,
	.read = kpagecgroup_read,
};
#endif /* CONFIG_MEMCG */

static int __init proc_page_init(void)
{
	proc_create("kpagecount", S_IRUSR, NULL, &proc_kpagecount_operations);
	proc_create("kpageflags", S_IRUSR, NULL, &proc_kpageflags_operations);
#ifdef CONFIG_MEMCG
	proc_create("kpagecgroup", S_IRUSR, NULL, &proc_kpagecgroup_operations);
#endif
	return 0;
}
fs_initcall(proc_page_init);
