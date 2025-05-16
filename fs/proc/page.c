// SPDX-License-Identifier: GPL-2.0
#include <linux/memblock.h>
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
#include <linux/memremap.h>
#include <linux/memcontrol.h>
#include <linux/mmu_notifier.h>
#include <linux/page_idle.h>
#include <linux/kernel-page-flags.h>
#include <linux/uaccess.h>
#include "internal.h"

#define KPMSIZE sizeof(u64)
#define KPMMASK (KPMSIZE - 1)
#define KPMBITS (KPMSIZE * BITS_PER_BYTE)

static inline unsigned long get_max_dump_pfn(void)
{
#ifdef CONFIG_SPARSEMEM
	/*
	 * The memmap of early sections is completely populated and marked
	 * online even if max_pfn does not fall on a section boundary -
	 * pfn_to_online_page() will succeed on all pages. Allow inspecting
	 * these memmaps.
	 */
	return round_up(max_pfn, PAGES_PER_SECTION);
#else
	return max_pfn;
#endif
}

/* /proc/kpagecount - an array exposing page mapcounts
 *
 * Each entry is a u64 representing the corresponding
 * physical page mapcount.
 */
static ssize_t kpagecount_read(struct file *file, char __user *buf,
			     size_t count, loff_t *ppos)
{
	const unsigned long max_dump_pfn = get_max_dump_pfn();
	u64 __user *out = (u64 __user *)buf;
	unsigned long src = *ppos;
	unsigned long pfn;
	ssize_t ret = 0;

	pfn = src / KPMSIZE;
	if (src & KPMMASK || count & KPMMASK)
		return -EINVAL;
	if (src >= max_dump_pfn * KPMSIZE)
		return 0;
	count = min_t(unsigned long, count, (max_dump_pfn * KPMSIZE) - src);

	while (count > 0) {
		struct page *page;
		u64 mapcount = 0;

		/*
		 * TODO: ZONE_DEVICE support requires to identify
		 * memmaps that were actually initialized.
		 */
		page = pfn_to_online_page(pfn);
		if (page) {
			struct folio *folio = page_folio(page);

			if (IS_ENABLED(CONFIG_PAGE_MAPCOUNT))
				mapcount = folio_precise_page_mapcount(folio, page);
			else
				mapcount = folio_average_page_mapcount(folio);
		}

		if (put_user(mapcount, out)) {
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

static const struct proc_ops kpagecount_proc_ops = {
	.proc_flags	= PROC_ENTRY_PERMANENT,
	.proc_lseek	= mem_lseek,
	.proc_read	= kpagecount_read,
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

u64 stable_page_flags(const struct page *page)
{
	const struct folio *folio;
	unsigned long k;
	unsigned long mapping;
	bool is_anon;
	u64 u = 0;

	/*
	 * pseudo flag: KPF_NOPAGE
	 * it differentiates a memory hole from a page with no flags
	 */
	if (!page)
		return 1 << KPF_NOPAGE;
	folio = page_folio(page);

	k = folio->flags;
	mapping = (unsigned long)folio->mapping;
	is_anon = mapping & PAGE_MAPPING_ANON;

	/*
	 * pseudo flags for the well known (anonymous) memory mapped pages
	 */
	if (page_mapped(page))
		u |= 1 << KPF_MMAP;
	if (is_anon) {
		u |= 1 << KPF_ANON;
		if (mapping & PAGE_MAPPING_KSM)
			u |= 1 << KPF_KSM;
	}

	/*
	 * compound pages: export both head/tail info
	 * they together define a compound page's start/end pos and order
	 */
	if (page == &folio->page)
		u |= kpf_copy_bit(k, KPF_COMPOUND_HEAD, PG_head);
	else
		u |= 1 << KPF_COMPOUND_TAIL;
	if (folio_test_hugetlb(folio))
		u |= 1 << KPF_HUGE;
	else if (folio_test_large(folio) &&
	         folio_test_large_rmappable(folio)) {
		/* Note: we indicate any THPs here, not just PMD-sized ones */
		u |= 1 << KPF_THP;
	} else if (is_huge_zero_folio(folio)) {
		u |= 1 << KPF_ZERO_PAGE;
		u |= 1 << KPF_THP;
	} else if (is_zero_folio(folio)) {
		u |= 1 << KPF_ZERO_PAGE;
	}

	/*
	 * Caveats on high order pages: PG_buddy and PG_slab will only be set
	 * on the head page.
	 */
	if (PageBuddy(page))
		u |= 1 << KPF_BUDDY;
	else if (page_count(page) == 0 && is_free_buddy_page(page))
		u |= 1 << KPF_BUDDY;

	if (PageOffline(page))
		u |= 1 << KPF_OFFLINE;
	if (PageTable(page))
		u |= 1 << KPF_PGTABLE;
	if (folio_test_slab(folio))
		u |= 1 << KPF_SLAB;

#if defined(CONFIG_PAGE_IDLE_FLAG) && defined(CONFIG_64BIT)
	u |= kpf_copy_bit(k, KPF_IDLE,          PG_idle);
#else
	if (folio_test_idle(folio))
		u |= 1 << KPF_IDLE;
#endif

	u |= kpf_copy_bit(k, KPF_LOCKED,	PG_locked);
	u |= kpf_copy_bit(k, KPF_DIRTY,		PG_dirty);
	u |= kpf_copy_bit(k, KPF_UPTODATE,	PG_uptodate);
	u |= kpf_copy_bit(k, KPF_WRITEBACK,	PG_writeback);

	u |= kpf_copy_bit(k, KPF_LRU,		PG_lru);
	u |= kpf_copy_bit(k, KPF_REFERENCED,	PG_referenced);
	u |= kpf_copy_bit(k, KPF_ACTIVE,	PG_active);
	u |= kpf_copy_bit(k, KPF_RECLAIM,	PG_reclaim);

#define SWAPCACHE ((1 << PG_swapbacked) | (1 << PG_swapcache))
	if ((k & SWAPCACHE) == SWAPCACHE)
		u |= 1 << KPF_SWAPCACHE;
	u |= kpf_copy_bit(k, KPF_SWAPBACKED,	PG_swapbacked);

	u |= kpf_copy_bit(k, KPF_UNEVICTABLE,	PG_unevictable);
	u |= kpf_copy_bit(k, KPF_MLOCKED,	PG_mlocked);

#ifdef CONFIG_MEMORY_FAILURE
	if (u & (1 << KPF_HUGE))
		u |= kpf_copy_bit(k, KPF_HWPOISON,	PG_hwpoison);
	else
		u |= kpf_copy_bit(page->flags, KPF_HWPOISON,	PG_hwpoison);
#endif

	u |= kpf_copy_bit(k, KPF_RESERVED,	PG_reserved);
	u |= kpf_copy_bit(k, KPF_OWNER_2,	PG_owner_2);
	u |= kpf_copy_bit(k, KPF_PRIVATE,	PG_private);
	u |= kpf_copy_bit(k, KPF_PRIVATE_2,	PG_private_2);
	u |= kpf_copy_bit(k, KPF_OWNER_PRIVATE,	PG_owner_priv_1);
	u |= kpf_copy_bit(k, KPF_ARCH,		PG_arch_1);
#ifdef CONFIG_ARCH_USES_PG_ARCH_2
	u |= kpf_copy_bit(k, KPF_ARCH_2,	PG_arch_2);
#endif
#ifdef CONFIG_ARCH_USES_PG_ARCH_3
	u |= kpf_copy_bit(k, KPF_ARCH_3,	PG_arch_3);
#endif

	return u;
};

static ssize_t kpageflags_read(struct file *file, char __user *buf,
			     size_t count, loff_t *ppos)
{
	const unsigned long max_dump_pfn = get_max_dump_pfn();
	u64 __user *out = (u64 __user *)buf;
	unsigned long src = *ppos;
	unsigned long pfn;
	ssize_t ret = 0;

	pfn = src / KPMSIZE;
	if (src & KPMMASK || count & KPMMASK)
		return -EINVAL;
	if (src >= max_dump_pfn * KPMSIZE)
		return 0;
	count = min_t(unsigned long, count, (max_dump_pfn * KPMSIZE) - src);

	while (count > 0) {
		/*
		 * TODO: ZONE_DEVICE support requires to identify
		 * memmaps that were actually initialized.
		 */
		struct page *page = pfn_to_online_page(pfn);

		if (put_user(stable_page_flags(page), out)) {
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

static const struct proc_ops kpageflags_proc_ops = {
	.proc_flags	= PROC_ENTRY_PERMANENT,
	.proc_lseek	= mem_lseek,
	.proc_read	= kpageflags_read,
};

#ifdef CONFIG_MEMCG
static ssize_t kpagecgroup_read(struct file *file, char __user *buf,
				size_t count, loff_t *ppos)
{
	const unsigned long max_dump_pfn = get_max_dump_pfn();
	u64 __user *out = (u64 __user *)buf;
	struct page *ppage;
	unsigned long src = *ppos;
	unsigned long pfn;
	ssize_t ret = 0;
	u64 ino;

	pfn = src / KPMSIZE;
	if (src & KPMMASK || count & KPMMASK)
		return -EINVAL;
	if (src >= max_dump_pfn * KPMSIZE)
		return 0;
	count = min_t(unsigned long, count, (max_dump_pfn * KPMSIZE) - src);

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

static const struct proc_ops kpagecgroup_proc_ops = {
	.proc_flags	= PROC_ENTRY_PERMANENT,
	.proc_lseek	= mem_lseek,
	.proc_read	= kpagecgroup_read,
};
#endif /* CONFIG_MEMCG */

static int __init proc_page_init(void)
{
	proc_create("kpagecount", S_IRUSR, NULL, &kpagecount_proc_ops);
	proc_create("kpageflags", S_IRUSR, NULL, &kpageflags_proc_ops);
#ifdef CONFIG_MEMCG
	proc_create("kpagecgroup", S_IRUSR, NULL, &kpagecgroup_proc_ops);
#endif
	return 0;
}
fs_initcall(proc_page_init);
