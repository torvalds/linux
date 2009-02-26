#include <linux/bootmem.h>
#include <linux/compiler.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
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
		ppage = NULL;
		if (pfn_valid(pfn))
			ppage = pfn_to_page(pfn);
		pfn++;
		if (!ppage)
			pcount = 0;
		else
			pcount = page_mapcount(ppage);

		if (put_user(pcount, out++)) {
			ret = -EFAULT;
			break;
		}

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

#define KPF_LOCKED     0
#define KPF_ERROR      1
#define KPF_REFERENCED 2
#define KPF_UPTODATE   3
#define KPF_DIRTY      4
#define KPF_LRU        5
#define KPF_ACTIVE     6
#define KPF_SLAB       7
#define KPF_WRITEBACK  8
#define KPF_RECLAIM    9
#define KPF_BUDDY     10

#define kpf_copy_bit(flags, srcpos, dstpos) (((flags >> srcpos) & 1) << dstpos)

static ssize_t kpageflags_read(struct file *file, char __user *buf,
			     size_t count, loff_t *ppos)
{
	u64 __user *out = (u64 __user *)buf;
	struct page *ppage;
	unsigned long src = *ppos;
	unsigned long pfn;
	ssize_t ret = 0;
	u64 kflags, uflags;

	pfn = src / KPMSIZE;
	count = min_t(unsigned long, count, (max_pfn * KPMSIZE) - src);
	if (src & KPMMASK || count & KPMMASK)
		return -EINVAL;

	while (count > 0) {
		ppage = NULL;
		if (pfn_valid(pfn))
			ppage = pfn_to_page(pfn);
		pfn++;
		if (!ppage)
			kflags = 0;
		else
			kflags = ppage->flags;

		uflags = kpf_copy_bit(kflags, KPF_LOCKED, PG_locked) |
			kpf_copy_bit(kflags, KPF_ERROR, PG_error) |
			kpf_copy_bit(kflags, KPF_REFERENCED, PG_referenced) |
			kpf_copy_bit(kflags, KPF_UPTODATE, PG_uptodate) |
			kpf_copy_bit(kflags, KPF_DIRTY, PG_dirty) |
			kpf_copy_bit(kflags, KPF_LRU, PG_lru) |
			kpf_copy_bit(kflags, KPF_ACTIVE, PG_active) |
			kpf_copy_bit(kflags, KPF_SLAB, PG_slab) |
			kpf_copy_bit(kflags, KPF_WRITEBACK, PG_writeback) |
			kpf_copy_bit(kflags, KPF_RECLAIM, PG_reclaim) |
			kpf_copy_bit(kflags, KPF_BUDDY, PG_buddy);

		if (put_user(uflags, out++)) {
			ret = -EFAULT;
			break;
		}

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
