/*
 *  linux/fs/proc/proc_misc.c
 *
 *  linux/fs/proc/array.c
 *  Copyright (C) 1992  by Linus Torvalds
 *  based on ideas by Darren Senn
 *
 *  This used to be the part of array.c. See the rest of history and credits
 *  there. I took this into a separate file and switched the thing to generic
 *  proc_file_inode_operations, leaving in array.c only per-process stuff.
 *  Inumbers allocation made dynamic (via create_proc_entry()).  AV, May 1999.
 *
 * Changes:
 * Fulton Green      :  Encapsulated position metric calculations.
 *			<kernel@FultonGreen.com>
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/fs.h>
#include <linux/tty.h>
#include <linux/string.h>
#include <linux/mman.h>
#include <linux/quicklist.h>
#include <linux/proc_fs.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/pagemap.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include <linux/genhd.h>
#include <linux/smp.h>
#include <linux/signal.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/seq_file.h>
#include <linux/times.h>
#include <linux/profile.h>
#include <linux/utsname.h>
#include <linux/blkdev.h>
#include <linux/hugetlb.h>
#include <linux/jiffies.h>
#include <linux/vmalloc.h>
#include <linux/crash_dump.h>
#include <linux/pid_namespace.h>
#include <linux/bootmem.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/tlb.h>
#include <asm/div64.h>
#include "internal.h"

static int zoneinfo_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &zoneinfo_op);
}

static const struct file_operations proc_zoneinfo_file_operations = {
	.open		= zoneinfo_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

#ifdef CONFIG_BLOCK
static int diskstats_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &diskstats_op);
}
static const struct file_operations proc_diskstats_operations = {
	.open		= diskstats_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};
#endif

#ifdef CONFIG_MODULES
extern const struct seq_operations modules_op;
static int modules_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &modules_op);
}
static const struct file_operations proc_modules_operations = {
	.open		= modules_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};
#endif

#ifdef CONFIG_PROC_PAGE_MONITOR
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

static struct file_operations proc_kpagecount_operations = {
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

		uflags = kpf_copy_bit(KPF_LOCKED, PG_locked, kflags) |
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

static struct file_operations proc_kpageflags_operations = {
	.llseek = mem_lseek,
	.read = kpageflags_read,
};
#endif /* CONFIG_PROC_PAGE_MONITOR */

struct proc_dir_entry *proc_root_kcore;

void __init proc_misc_init(void)
{
	proc_symlink("mounts", NULL, "self/mounts");

	/* And now for trickier ones */
	proc_create("zoneinfo", S_IRUGO, NULL, &proc_zoneinfo_file_operations);
#ifdef CONFIG_BLOCK
	proc_create("diskstats", 0, NULL, &proc_diskstats_operations);
#endif
#ifdef CONFIG_MODULES
	proc_create("modules", 0, NULL, &proc_modules_operations);
#endif
#ifdef CONFIG_SCHEDSTATS
	proc_create("schedstat", 0, NULL, &proc_schedstat_operations);
#endif
#ifdef CONFIG_PROC_KCORE
	proc_root_kcore = proc_create("kcore", S_IRUSR, NULL, &proc_kcore_operations);
	if (proc_root_kcore)
		proc_root_kcore->size =
				(size_t)high_memory - PAGE_OFFSET + PAGE_SIZE;
#endif
#ifdef CONFIG_PROC_PAGE_MONITOR
	proc_create("kpagecount", S_IRUSR, NULL, &proc_kpagecount_operations);
	proc_create("kpageflags", S_IRUSR, NULL, &proc_kpageflags_operations);
#endif
#ifdef CONFIG_PROC_VMCORE
	proc_vmcore = proc_create("vmcore", S_IRUSR, NULL, &proc_vmcore_operations);
#endif
}
