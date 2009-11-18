#include <linux/bootmem.h>
#include <linux/compiler.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ksm.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/hugetlb.h>
#include <asm/uaccess.h>
#include "internal.h"

#define KPMSIZE sizeof(u64)
#define KPMMASK (KPMSIZE - 1)

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
		if (pfn_valid(pfn))
			ppage = pfn_to_page(pfn);
		else
			ppage = NULL;
		if (!ppage)
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

/* These macros are used to decouple internal flags from exported ones */

#define KPF_LOCKED		0
#define KPF_ERROR		1
#define KPF_REFERENCED		2
#define KPF_UPTODATE		3
#define KPF_DIRTY		4
#define KPF_LRU			5
#define KPF_ACTIVE		6
#define KPF_SLAB		7
#define KPF_WRITEBACK		8
#define KPF_RECLAIM		9
#define KPF_BUDDY		10

/* 11-20: new additions in 2.6.31 */
#define KPF_MMAP		11
#define KPF_ANON		12
#define KPF_SWAPCACHE		13
#define KPF_SWAPBACKED		14
#define KPF_COMPOUND_HEAD	15
#define KPF_COMPOUND_TAIL	16
#define KPF_HUGE		17
#define KPF_UNEVICTABLE		18
#define KPF_HWPOISON		19
#define KPF_NOPAGE		20

#define KPF_KSM			21

/* kernel hacking assistances
 * WARNING: subject to change, never rely on them!
 */
#define KPF_RESERVED		32
#define KPF_MLOCKED		33
#define KPF_MAPPEDTODISK	34
#define KPF_PRIVATE		35
#define KPF_PRIVATE_2		36
#define KPF_OWNER_PRIVATE	37
#define KPF_ARCH		38
#define KPF_UNCACHED		39

static inline u64 kpf_copy_bit(u64 kflags, int ubit, int kbit)
{
	return ((kflags >> kbit) & 1) << ubit;
}

static u64 get_uflags(struct page *page)
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

	u |= kpf_copy_bit(k, KPF_LOCKED,	PG_locked);

	/*
	 * Caveats on high order pages:
	 * PG_buddy will only be set on the head page; SLUB/SLQB do the same
	 * for PG_slab; SLOB won't set PG_slab at all on compound pages.
	 */
	u |= kpf_copy_bit(k, KPF_SLAB,		PG_slab);
	u |= kpf_copy_bit(k, KPF_BUDDY,		PG_buddy);

	u |= kpf_copy_bit(k, KPF_ERROR,		PG_error);
	u |= kpf_copy_bit(k, KPF_DIRTY,		PG_dirty);
	u |= kpf_copy_bit(k, KPF_UPTODATE,	PG_uptodate);
	u |= kpf_copy_bit(k, KPF_WRITEBACK,	PG_writeback);

	u |= kpf_copy_bit(k, KPF_LRU,		PG_lru);
	u |= kpf_copy_bit(k, KPF_REFERENCED,	PG_referenced);
	u |= kpf_copy_bit(k, KPF_ACTIVE,	PG_active);
	u |= kpf_copy_bit(k, KPF_RECLAIM,	PG_reclaim);

	u |= kpf_copy_bit(k, KPF_SWAPCACHE,	PG_swapcache);
	u |= kpf_copy_bit(k, KPF_SWAPBACKED,	PG_swapbacked);

	u |= kpf_copy_bit(k, KPF_UNEVICTABLE,	PG_unevictable);
	u |= kpf_copy_bit(k, KPF_MLOCKED,	PG_mlocked);

#ifdef CONFIG_MEMORY_FAILURE
	u |= kpf_copy_bit(k, KPF_HWPOISON,	PG_hwpoison);
#endif

#ifdef CONFIG_IA64_UNCACHED_ALLOCATOR
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
		if (pfn_valid(pfn))
			ppage = pfn_to_page(pfn);
		else
			ppage = NULL;

		if (put_user(get_uflags(ppage), out)) {
			ret = -EFAULT;
			break;
		}

		pfn++;
		out++;
		count -= KPMSIZE;
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

static int __init proc_page_init(void)
{
	proc_create("kpagecount", S_IRUSR, NULL, &proc_kpagecount_operations);
	proc_create("kpageflags", S_IRUSR, NULL, &proc_kpageflags_operations);
	return 0;
}
module_init(proc_page_init);
