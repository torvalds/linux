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
#include <linux/sysrq.h>
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

#define LOAD_INT(x) ((x) >> FSHIFT)
#define LOAD_FRAC(x) LOAD_INT(((x) & (FIXED_1-1)) * 100)
/*
 * Warning: stuff below (imported functions) assumes that its output will fit
 * into one page. For some of those functions it may be wrong. Moreover, we
 * have a way to deal with that gracefully. Right now I used straightforward
 * wrappers, but this needs further analysis wrt potential overflows.
 */
extern int get_hardware_list(char *);
extern int get_stram_list(char *);
extern int get_exec_domain_list(char *);

static int proc_calc_metrics(char *page, char **start, off_t off,
				 int count, int *eof, int len)
{
	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count) len = count;
	if (len<0) len = 0;
	return len;
}

static int loadavg_read_proc(char *page, char **start, off_t off,
				 int count, int *eof, void *data)
{
	int a, b, c;
	int len;
	unsigned long seq;

	do {
		seq = read_seqbegin(&xtime_lock);
		a = avenrun[0] + (FIXED_1/200);
		b = avenrun[1] + (FIXED_1/200);
		c = avenrun[2] + (FIXED_1/200);
	} while (read_seqretry(&xtime_lock, seq));

	len = sprintf(page,"%d.%02d %d.%02d %d.%02d %ld/%d %d\n",
		LOAD_INT(a), LOAD_FRAC(a),
		LOAD_INT(b), LOAD_FRAC(b),
		LOAD_INT(c), LOAD_FRAC(c),
		nr_running(), nr_threads,
		task_active_pid_ns(current)->last_pid);
	return proc_calc_metrics(page, start, off, count, eof, len);
}

static int uptime_read_proc(char *page, char **start, off_t off,
				 int count, int *eof, void *data)
{
	struct timespec uptime;
	struct timespec idle;
	int len;
	cputime_t idletime = cputime_add(init_task.utime, init_task.stime);

	do_posix_clock_monotonic_gettime(&uptime);
	monotonic_to_bootbased(&uptime);
	cputime_to_timespec(idletime, &idle);
	len = sprintf(page,"%lu.%02lu %lu.%02lu\n",
			(unsigned long) uptime.tv_sec,
			(uptime.tv_nsec / (NSEC_PER_SEC / 100)),
			(unsigned long) idle.tv_sec,
			(idle.tv_nsec / (NSEC_PER_SEC / 100)));

	return proc_calc_metrics(page, start, off, count, eof, len);
}

int __attribute__((weak)) arch_report_meminfo(char *page)
{
	return 0;
}

static int meminfo_read_proc(char *page, char **start, off_t off,
				 int count, int *eof, void *data)
{
	struct sysinfo i;
	int len;
	unsigned long committed;
	unsigned long allowed;
	struct vmalloc_info vmi;
	long cached;

/*
 * display in kilobytes.
 */
#define K(x) ((x) << (PAGE_SHIFT - 10))
	si_meminfo(&i);
	si_swapinfo(&i);
	committed = atomic_long_read(&vm_committed_space);
	allowed = ((totalram_pages - hugetlb_total_pages())
		* sysctl_overcommit_ratio / 100) + total_swap_pages;

	cached = global_page_state(NR_FILE_PAGES) -
			total_swapcache_pages - i.bufferram;
	if (cached < 0)
		cached = 0;

	get_vmalloc_info(&vmi);

	/*
	 * Tagged format, for easy grepping and expansion.
	 */
	len = sprintf(page,
		"MemTotal:     %8lu kB\n"
		"MemFree:      %8lu kB\n"
		"Buffers:      %8lu kB\n"
		"Cached:       %8lu kB\n"
		"SwapCached:   %8lu kB\n"
		"Active:       %8lu kB\n"
		"Inactive:     %8lu kB\n"
#ifdef CONFIG_HIGHMEM
		"HighTotal:    %8lu kB\n"
		"HighFree:     %8lu kB\n"
		"LowTotal:     %8lu kB\n"
		"LowFree:      %8lu kB\n"
#endif
		"SwapTotal:    %8lu kB\n"
		"SwapFree:     %8lu kB\n"
		"Dirty:        %8lu kB\n"
		"Writeback:    %8lu kB\n"
		"AnonPages:    %8lu kB\n"
		"Mapped:       %8lu kB\n"
		"Slab:         %8lu kB\n"
		"SReclaimable: %8lu kB\n"
		"SUnreclaim:   %8lu kB\n"
		"PageTables:   %8lu kB\n"
#ifdef CONFIG_QUICKLIST
		"Quicklists:   %8lu kB\n"
#endif
		"NFS_Unstable: %8lu kB\n"
		"Bounce:       %8lu kB\n"
		"WritebackTmp: %8lu kB\n"
		"CommitLimit:  %8lu kB\n"
		"Committed_AS: %8lu kB\n"
		"VmallocTotal: %8lu kB\n"
		"VmallocUsed:  %8lu kB\n"
		"VmallocChunk: %8lu kB\n",
		K(i.totalram),
		K(i.freeram),
		K(i.bufferram),
		K(cached),
		K(total_swapcache_pages),
		K(global_page_state(NR_ACTIVE)),
		K(global_page_state(NR_INACTIVE)),
#ifdef CONFIG_HIGHMEM
		K(i.totalhigh),
		K(i.freehigh),
		K(i.totalram-i.totalhigh),
		K(i.freeram-i.freehigh),
#endif
		K(i.totalswap),
		K(i.freeswap),
		K(global_page_state(NR_FILE_DIRTY)),
		K(global_page_state(NR_WRITEBACK)),
		K(global_page_state(NR_ANON_PAGES)),
		K(global_page_state(NR_FILE_MAPPED)),
		K(global_page_state(NR_SLAB_RECLAIMABLE) +
				global_page_state(NR_SLAB_UNRECLAIMABLE)),
		K(global_page_state(NR_SLAB_RECLAIMABLE)),
		K(global_page_state(NR_SLAB_UNRECLAIMABLE)),
		K(global_page_state(NR_PAGETABLE)),
#ifdef CONFIG_QUICKLIST
		K(quicklist_total_size()),
#endif
		K(global_page_state(NR_UNSTABLE_NFS)),
		K(global_page_state(NR_BOUNCE)),
		K(global_page_state(NR_WRITEBACK_TEMP)),
		K(allowed),
		K(committed),
		(unsigned long)VMALLOC_TOTAL >> 10,
		vmi.used >> 10,
		vmi.largest_chunk >> 10
		);

		len += hugetlb_report_meminfo(page + len);

	len += arch_report_meminfo(page + len);

	return proc_calc_metrics(page, start, off, count, eof, len);
#undef K
}

static int fragmentation_open(struct inode *inode, struct file *file)
{
	(void)inode;
	return seq_open(file, &fragmentation_op);
}

static const struct file_operations fragmentation_file_operations = {
	.open		= fragmentation_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int pagetypeinfo_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &pagetypeinfo_op);
}

static const struct file_operations pagetypeinfo_file_ops = {
	.open		= pagetypeinfo_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

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

static int version_read_proc(char *page, char **start, off_t off,
				 int count, int *eof, void *data)
{
	int len;

	len = snprintf(page, PAGE_SIZE, linux_proc_banner,
		utsname()->sysname,
		utsname()->release,
		utsname()->version);
	return proc_calc_metrics(page, start, off, count, eof, len);
}

extern const struct seq_operations cpuinfo_op;
static int cpuinfo_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &cpuinfo_op);
}

static const struct file_operations proc_cpuinfo_operations = {
	.open		= cpuinfo_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int devinfo_show(struct seq_file *f, void *v)
{
	int i = *(loff_t *) v;

	if (i < CHRDEV_MAJOR_HASH_SIZE) {
		if (i == 0)
			seq_printf(f, "Character devices:\n");
		chrdev_show(f, i);
	}
#ifdef CONFIG_BLOCK
	else {
		i -= CHRDEV_MAJOR_HASH_SIZE;
		if (i == 0)
			seq_printf(f, "\nBlock devices:\n");
		blkdev_show(f, i);
	}
#endif
	return 0;
}

static void *devinfo_start(struct seq_file *f, loff_t *pos)
{
	if (*pos < (BLKDEV_MAJOR_HASH_SIZE + CHRDEV_MAJOR_HASH_SIZE))
		return pos;
	return NULL;
}

static void *devinfo_next(struct seq_file *f, void *v, loff_t *pos)
{
	(*pos)++;
	if (*pos >= (BLKDEV_MAJOR_HASH_SIZE + CHRDEV_MAJOR_HASH_SIZE))
		return NULL;
	return pos;
}

static void devinfo_stop(struct seq_file *f, void *v)
{
	/* Nothing to do */
}

static const struct seq_operations devinfo_ops = {
	.start = devinfo_start,
	.next  = devinfo_next,
	.stop  = devinfo_stop,
	.show  = devinfo_show
};

static int devinfo_open(struct inode *inode, struct file *filp)
{
	return seq_open(filp, &devinfo_ops);
}

static const struct file_operations proc_devinfo_operations = {
	.open		= devinfo_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int vmstat_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &vmstat_op);
}
static const struct file_operations proc_vmstat_file_operations = {
	.open		= vmstat_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

#ifdef CONFIG_PROC_HARDWARE
static int hardware_read_proc(char *page, char **start, off_t off,
				 int count, int *eof, void *data)
{
	int len = get_hardware_list(page);
	return proc_calc_metrics(page, start, off, count, eof, len);
}
#endif

#ifdef CONFIG_STRAM_PROC
static int stram_read_proc(char *page, char **start, off_t off,
				 int count, int *eof, void *data)
{
	int len = get_stram_list(page);
	return proc_calc_metrics(page, start, off, count, eof, len);
}
#endif

#ifdef CONFIG_BLOCK
static int partitions_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &partitions_op);
}
static const struct file_operations proc_partitions_operations = {
	.open		= partitions_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

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

#ifdef CONFIG_SLABINFO
static int slabinfo_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &slabinfo_op);
}
static const struct file_operations proc_slabinfo_operations = {
	.open		= slabinfo_open,
	.read		= seq_read,
	.write		= slabinfo_write,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

#ifdef CONFIG_DEBUG_SLAB_LEAK
extern const struct seq_operations slabstats_op;
static int slabstats_open(struct inode *inode, struct file *file)
{
	unsigned long *n = kzalloc(PAGE_SIZE, GFP_KERNEL);
	int ret = -ENOMEM;
	if (n) {
		ret = seq_open(file, &slabstats_op);
		if (!ret) {
			struct seq_file *m = file->private_data;
			*n = PAGE_SIZE / (2 * sizeof(unsigned long));
			m->private = n;
			n = NULL;
		}
		kfree(n);
	}
	return ret;
}

static const struct file_operations proc_slabstats_operations = {
	.open		= slabstats_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release_private,
};
#endif
#endif

#ifdef CONFIG_MMU
static int vmalloc_open(struct inode *inode, struct file *file)
{
	unsigned int *ptr = NULL;
	int ret;

	if (NUMA_BUILD)
		ptr = kmalloc(nr_node_ids * sizeof(unsigned int), GFP_KERNEL);
	ret = seq_open(file, &vmalloc_op);
	if (!ret) {
		struct seq_file *m = file->private_data;
		m->private = ptr;
	} else
		kfree(ptr);
	return ret;
}

static const struct file_operations proc_vmalloc_operations = {
	.open		= vmalloc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release_private,
};
#endif

#ifndef arch_irq_stat_cpu
#define arch_irq_stat_cpu(cpu) 0
#endif
#ifndef arch_irq_stat
#define arch_irq_stat() 0
#endif

static int show_stat(struct seq_file *p, void *v)
{
	int i;
	unsigned long jif;
	cputime64_t user, nice, system, idle, iowait, irq, softirq, steal;
	cputime64_t guest;
	u64 sum = 0;
	struct timespec boottime;
	unsigned int *per_irq_sum;

	per_irq_sum = kzalloc(sizeof(unsigned int)*NR_IRQS, GFP_KERNEL);
	if (!per_irq_sum)
		return -ENOMEM;

	user = nice = system = idle = iowait =
		irq = softirq = steal = cputime64_zero;
	guest = cputime64_zero;
	getboottime(&boottime);
	jif = boottime.tv_sec;

	for_each_possible_cpu(i) {
		int j;

		user = cputime64_add(user, kstat_cpu(i).cpustat.user);
		nice = cputime64_add(nice, kstat_cpu(i).cpustat.nice);
		system = cputime64_add(system, kstat_cpu(i).cpustat.system);
		idle = cputime64_add(idle, kstat_cpu(i).cpustat.idle);
		iowait = cputime64_add(iowait, kstat_cpu(i).cpustat.iowait);
		irq = cputime64_add(irq, kstat_cpu(i).cpustat.irq);
		softirq = cputime64_add(softirq, kstat_cpu(i).cpustat.softirq);
		steal = cputime64_add(steal, kstat_cpu(i).cpustat.steal);
		guest = cputime64_add(guest, kstat_cpu(i).cpustat.guest);
		for (j = 0; j < NR_IRQS; j++) {
			unsigned int temp = kstat_cpu(i).irqs[j];
			sum += temp;
			per_irq_sum[j] += temp;
		}
		sum += arch_irq_stat_cpu(i);
	}
	sum += arch_irq_stat();

	seq_printf(p, "cpu  %llu %llu %llu %llu %llu %llu %llu %llu %llu\n",
		(unsigned long long)cputime64_to_clock_t(user),
		(unsigned long long)cputime64_to_clock_t(nice),
		(unsigned long long)cputime64_to_clock_t(system),
		(unsigned long long)cputime64_to_clock_t(idle),
		(unsigned long long)cputime64_to_clock_t(iowait),
		(unsigned long long)cputime64_to_clock_t(irq),
		(unsigned long long)cputime64_to_clock_t(softirq),
		(unsigned long long)cputime64_to_clock_t(steal),
		(unsigned long long)cputime64_to_clock_t(guest));
	for_each_online_cpu(i) {

		/* Copy values here to work around gcc-2.95.3, gcc-2.96 */
		user = kstat_cpu(i).cpustat.user;
		nice = kstat_cpu(i).cpustat.nice;
		system = kstat_cpu(i).cpustat.system;
		idle = kstat_cpu(i).cpustat.idle;
		iowait = kstat_cpu(i).cpustat.iowait;
		irq = kstat_cpu(i).cpustat.irq;
		softirq = kstat_cpu(i).cpustat.softirq;
		steal = kstat_cpu(i).cpustat.steal;
		guest = kstat_cpu(i).cpustat.guest;
		seq_printf(p,
			"cpu%d %llu %llu %llu %llu %llu %llu %llu %llu %llu\n",
			i,
			(unsigned long long)cputime64_to_clock_t(user),
			(unsigned long long)cputime64_to_clock_t(nice),
			(unsigned long long)cputime64_to_clock_t(system),
			(unsigned long long)cputime64_to_clock_t(idle),
			(unsigned long long)cputime64_to_clock_t(iowait),
			(unsigned long long)cputime64_to_clock_t(irq),
			(unsigned long long)cputime64_to_clock_t(softirq),
			(unsigned long long)cputime64_to_clock_t(steal),
			(unsigned long long)cputime64_to_clock_t(guest));
	}
	seq_printf(p, "intr %llu", (unsigned long long)sum);

	for (i = 0; i < NR_IRQS; i++)
		seq_printf(p, " %u", per_irq_sum[i]);

	seq_printf(p,
		"\nctxt %llu\n"
		"btime %lu\n"
		"processes %lu\n"
		"procs_running %lu\n"
		"procs_blocked %lu\n",
		nr_context_switches(),
		(unsigned long)jif,
		total_forks,
		nr_running(),
		nr_iowait());

	kfree(per_irq_sum);
	return 0;
}

static int stat_open(struct inode *inode, struct file *file)
{
	unsigned size = 4096 * (1 + num_possible_cpus() / 32);
	char *buf;
	struct seq_file *m;
	int res;

	/* don't ask for more than the kmalloc() max size, currently 128 KB */
	if (size > 128 * 1024)
		size = 128 * 1024;
	buf = kmalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	res = single_open(file, show_stat, NULL);
	if (!res) {
		m = file->private_data;
		m->buf = buf;
		m->size = size;
	} else
		kfree(buf);
	return res;
}
static const struct file_operations proc_stat_operations = {
	.open		= stat_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/*
 * /proc/interrupts
 */
static void *int_seq_start(struct seq_file *f, loff_t *pos)
{
	return (*pos <= NR_IRQS) ? pos : NULL;
}

static void *int_seq_next(struct seq_file *f, void *v, loff_t *pos)
{
	(*pos)++;
	if (*pos > NR_IRQS)
		return NULL;
	return pos;
}

static void int_seq_stop(struct seq_file *f, void *v)
{
	/* Nothing to do */
}


static const struct seq_operations int_seq_ops = {
	.start = int_seq_start,
	.next  = int_seq_next,
	.stop  = int_seq_stop,
	.show  = show_interrupts
};

static int interrupts_open(struct inode *inode, struct file *filp)
{
	return seq_open(filp, &int_seq_ops);
}

static const struct file_operations proc_interrupts_operations = {
	.open		= interrupts_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int filesystems_read_proc(char *page, char **start, off_t off,
				 int count, int *eof, void *data)
{
	int len = get_filesystem_list(page);
	return proc_calc_metrics(page, start, off, count, eof, len);
}

static int cmdline_read_proc(char *page, char **start, off_t off,
				 int count, int *eof, void *data)
{
	int len;

	len = sprintf(page, "%s\n", saved_command_line);
	return proc_calc_metrics(page, start, off, count, eof, len);
}

static int locks_open(struct inode *inode, struct file *filp)
{
	return seq_open(filp, &locks_seq_operations);
}

static const struct file_operations proc_locks_operations = {
	.open		= locks_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int execdomains_read_proc(char *page, char **start, off_t off,
				 int count, int *eof, void *data)
{
	int len = get_exec_domain_list(page);
	return proc_calc_metrics(page, start, off, count, eof, len);
}

#ifdef CONFIG_MAGIC_SYSRQ
/*
 * writing 'C' to /proc/sysrq-trigger is like sysrq-C
 */
static ssize_t write_sysrq_trigger(struct file *file, const char __user *buf,
				   size_t count, loff_t *ppos)
{
	if (count) {
		char c;

		if (get_user(c, buf))
			return -EFAULT;
		__handle_sysrq(c, NULL, 0);
	}
	return count;
}

static const struct file_operations proc_sysrq_trigger_operations = {
	.write		= write_sysrq_trigger,
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
	static struct {
		char *name;
		int (*read_proc)(char*,char**,off_t,int,int*,void*);
	} *p, simple_ones[] = {
		{"loadavg",     loadavg_read_proc},
		{"uptime",	uptime_read_proc},
		{"meminfo",	meminfo_read_proc},
		{"version",	version_read_proc},
#ifdef CONFIG_PROC_HARDWARE
		{"hardware",	hardware_read_proc},
#endif
#ifdef CONFIG_STRAM_PROC
		{"stram",	stram_read_proc},
#endif
		{"filesystems",	filesystems_read_proc},
		{"cmdline",	cmdline_read_proc},
		{"execdomains",	execdomains_read_proc},
		{NULL,}
	};
	for (p = simple_ones; p->name; p++)
		create_proc_read_entry(p->name, 0, NULL, p->read_proc, NULL);

	proc_symlink("mounts", NULL, "self/mounts");

	/* And now for trickier ones */
#ifdef CONFIG_PRINTK
	proc_create("kmsg", S_IRUSR, NULL, &proc_kmsg_operations);
#endif
	proc_create("locks", 0, NULL, &proc_locks_operations);
	proc_create("devices", 0, NULL, &proc_devinfo_operations);
	proc_create("cpuinfo", 0, NULL, &proc_cpuinfo_operations);
#ifdef CONFIG_BLOCK
	proc_create("partitions", 0, NULL, &proc_partitions_operations);
#endif
	proc_create("stat", 0, NULL, &proc_stat_operations);
	proc_create("interrupts", 0, NULL, &proc_interrupts_operations);
#ifdef CONFIG_SLABINFO
	proc_create("slabinfo",S_IWUSR|S_IRUGO,NULL,&proc_slabinfo_operations);
#ifdef CONFIG_DEBUG_SLAB_LEAK
	proc_create("slab_allocators", 0, NULL, &proc_slabstats_operations);
#endif
#endif
#ifdef CONFIG_MMU
	proc_create("vmallocinfo", S_IRUSR, NULL, &proc_vmalloc_operations);
#endif
	proc_create("buddyinfo", S_IRUGO, NULL, &fragmentation_file_operations);
	proc_create("pagetypeinfo", S_IRUGO, NULL, &pagetypeinfo_file_ops);
	proc_create("vmstat", S_IRUGO, NULL, &proc_vmstat_file_operations);
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
#ifdef CONFIG_MAGIC_SYSRQ
	proc_create("sysrq-trigger", S_IWUSR, NULL, &proc_sysrq_trigger_operations);
#endif
}
