#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/mman.h>
#include <linux/mmzone.h>
#include <linux/proc_fs.h>
#include <linux/quicklist.h>
#include <linux/seq_file.h>
#include <linux/swap.h>
#include <linux/vmstat.h>
#include <linux/atomic.h>
#include <linux/vmalloc.h>
#ifdef CONFIG_CMA
#include <linux/cma.h>
#endif
#include <asm/page.h>
#include <asm/pgtable.h>
#include "internal.h"

void __attribute__((weak)) arch_report_meminfo(struct seq_file *m)
{
}

static int meminfo_proc_show(struct seq_file *m, void *v)
{
	struct sysinfo i;
	unsigned long committed;
	long cached;
	long available;
	unsigned long pages[NR_LRU_LISTS];
	int lru;

/*
 * display in kilobytes.
 */
#define K(x) ((x) << (PAGE_SHIFT - 10))
	si_meminfo(&i);
	si_swapinfo(&i);
	committed = percpu_counter_read_positive(&vm_committed_as);

	cached = global_page_state(NR_FILE_PAGES) -
			total_swapcache_pages() - i.bufferram;
	if (cached < 0)
		cached = 0;

	for (lru = LRU_BASE; lru < NR_LRU_LISTS; lru++)
		pages[lru] = global_page_state(NR_LRU_BASE + lru);

	available = si_mem_available();

	/*
	 * Tagged format, for easy grepping and expansion.
	 */
	seq_printf(m,
		"MemTotal:       %8lu kB\n"
		"MemFree:        %8lu kB\n"
		"MemAvailable:   %8lu kB\n"
		"Buffers:        %8lu kB\n"
		"Cached:         %8lu kB\n"
		"SwapCached:     %8lu kB\n"
		"Active:         %8lu kB\n"
		"Inactive:       %8lu kB\n"
		"Active(anon):   %8lu kB\n"
		"Inactive(anon): %8lu kB\n"
		"Active(file):   %8lu kB\n"
		"Inactive(file): %8lu kB\n"
		"Unevictable:    %8lu kB\n"
		"Mlocked:        %8lu kB\n"
#ifdef CONFIG_HIGHMEM
		"HighTotal:      %8lu kB\n"
		"HighFree:       %8lu kB\n"
		"LowTotal:       %8lu kB\n"
		"LowFree:        %8lu kB\n"
#endif
#ifndef CONFIG_MMU
		"MmapCopy:       %8lu kB\n"
#endif
		"SwapTotal:      %8lu kB\n"
		"SwapFree:       %8lu kB\n"
		"Dirty:          %8lu kB\n"
		"Writeback:      %8lu kB\n"
		"AnonPages:      %8lu kB\n"
		"Mapped:         %8lu kB\n"
		"Shmem:          %8lu kB\n"
		"Slab:           %8lu kB\n"
		"SReclaimable:   %8lu kB\n"
		"SUnreclaim:     %8lu kB\n"
		"KernelStack:    %8lu kB\n"
		"PageTables:     %8lu kB\n"
#ifdef CONFIG_QUICKLIST
		"Quicklists:     %8lu kB\n"
#endif
		"NFS_Unstable:   %8lu kB\n"
		"Bounce:         %8lu kB\n"
		"WritebackTmp:   %8lu kB\n"
		"CommitLimit:    %8lu kB\n"
		"Committed_AS:   %8lu kB\n"
		"VmallocTotal:   %8lu kB\n"
		"VmallocUsed:    %8lu kB\n"
		"VmallocChunk:   %8lu kB\n"
#ifdef CONFIG_MEMORY_FAILURE
		"HardwareCorrupted: %5lu kB\n"
#endif
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
		"AnonHugePages:  %8lu kB\n"
#endif
#ifdef CONFIG_CMA
		"CmaTotal:       %8lu kB\n"
		"CmaFree:        %8lu kB\n"
#endif
		,
		K(i.totalram),
		K(i.freeram),
		K(available),
		K(i.bufferram),
		K(cached),
		K(total_swapcache_pages()),
		K(pages[LRU_ACTIVE_ANON]   + pages[LRU_ACTIVE_FILE]),
		K(pages[LRU_INACTIVE_ANON] + pages[LRU_INACTIVE_FILE]),
		K(pages[LRU_ACTIVE_ANON]),
		K(pages[LRU_INACTIVE_ANON]),
		K(pages[LRU_ACTIVE_FILE]),
		K(pages[LRU_INACTIVE_FILE]),
		K(pages[LRU_UNEVICTABLE]),
		K(global_page_state(NR_MLOCK)),
#ifdef CONFIG_HIGHMEM
		K(i.totalhigh),
		K(i.freehigh),
		K(i.totalram-i.totalhigh),
		K(i.freeram-i.freehigh),
#endif
#ifndef CONFIG_MMU
		K((unsigned long) atomic_long_read(&mmap_pages_allocated)),
#endif
		K(i.totalswap),
		K(i.freeswap),
		K(global_page_state(NR_FILE_DIRTY)),
		K(global_page_state(NR_WRITEBACK)),
		K(global_page_state(NR_ANON_PAGES)),
		K(global_page_state(NR_FILE_MAPPED)),
		K(i.sharedram),
		K(global_page_state(NR_SLAB_RECLAIMABLE) +
				global_page_state(NR_SLAB_UNRECLAIMABLE)),
		K(global_page_state(NR_SLAB_RECLAIMABLE)),
		K(global_page_state(NR_SLAB_UNRECLAIMABLE)),
		global_page_state(NR_KERNEL_STACK) * THREAD_SIZE / 1024,
		K(global_page_state(NR_PAGETABLE)),
#ifdef CONFIG_QUICKLIST
		K(quicklist_total_size()),
#endif
		K(global_page_state(NR_UNSTABLE_NFS)),
		K(global_page_state(NR_BOUNCE)),
		K(global_page_state(NR_WRITEBACK_TEMP)),
		K(vm_commit_limit()),
		K(committed),
		(unsigned long)VMALLOC_TOTAL >> 10,
		0ul, // used to be vmalloc 'used'
		0ul  // used to be vmalloc 'largest_chunk'
#ifdef CONFIG_MEMORY_FAILURE
		, atomic_long_read(&num_poisoned_pages) << (PAGE_SHIFT - 10)
#endif
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
		, K(global_page_state(NR_ANON_TRANSPARENT_HUGEPAGES) *
		   HPAGE_PMD_NR)
#endif
#ifdef CONFIG_CMA
		, K(totalcma_pages)
		, K(global_page_state(NR_FREE_CMA_PAGES))
#endif
		);

	hugetlb_report_meminfo(m);

	arch_report_meminfo(m);

	return 0;
#undef K
}

static int meminfo_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, meminfo_proc_show, NULL);
}

static const struct file_operations meminfo_proc_fops = {
	.open		= meminfo_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init proc_meminfo_init(void)
{
	proc_create("meminfo", 0, NULL, &meminfo_proc_fops);
	return 0;
}
fs_initcall(proc_meminfo_init);
