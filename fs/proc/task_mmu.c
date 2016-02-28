#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/huge_mm.h>
#include <linux/mount.h>
#include <linux/seq_file.h>
#include <linux/highmem.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/mempolicy.h>
#include <linux/rmap.h>
#include <linux/swap.h>
#include <linux/swapops.h>

#include <asm/elf.h>
#include <asm/uaccess.h>
#include <asm/tlbflush.h>
#include "internal.h"

void task_mem(struct seq_file *m, struct mm_struct *mm)
{
	unsigned long data, text, lib, swap;
	unsigned long hiwater_vm, total_vm, hiwater_rss, total_rss;

	/*
	 * Note: to minimize their overhead, mm maintains hiwater_vm and
	 * hiwater_rss only when about to *lower* total_vm or rss.  Any
	 * collector of these hiwater stats must therefore get total_vm
	 * and rss too, which will usually be the higher.  Barriers? not
	 * worth the effort, such snapshots can always be inconsistent.
	 */
	hiwater_vm = total_vm = mm->total_vm;
	if (hiwater_vm < mm->hiwater_vm)
		hiwater_vm = mm->hiwater_vm;
	hiwater_rss = total_rss = get_mm_rss(mm);
	if (hiwater_rss < mm->hiwater_rss)
		hiwater_rss = mm->hiwater_rss;

	data = mm->total_vm - mm->shared_vm - mm->stack_vm;
	text = (PAGE_ALIGN(mm->end_code) - (mm->start_code & PAGE_MASK)) >> 10;
	lib = (mm->exec_vm << (PAGE_SHIFT-10)) - text;
	swap = get_mm_counter(mm, MM_SWAPENTS);
	seq_printf(m,
		"VmPeak:\t%8lu kB\n"
		"VmSize:\t%8lu kB\n"
		"VmLck:\t%8lu kB\n"
		"VmPin:\t%8lu kB\n"
		"VmHWM:\t%8lu kB\n"
		"VmRSS:\t%8lu kB\n"
		"VmData:\t%8lu kB\n"
		"VmStk:\t%8lu kB\n"
		"VmExe:\t%8lu kB\n"
		"VmLib:\t%8lu kB\n"
		"VmPTE:\t%8lu kB\n"
		"VmSwap:\t%8lu kB\n",
		hiwater_vm << (PAGE_SHIFT-10),
		total_vm << (PAGE_SHIFT-10),
		mm->locked_vm << (PAGE_SHIFT-10),
		mm->pinned_vm << (PAGE_SHIFT-10),
		hiwater_rss << (PAGE_SHIFT-10),
		total_rss << (PAGE_SHIFT-10),
		data << (PAGE_SHIFT-10),
		mm->stack_vm << (PAGE_SHIFT-10), text, lib,
		(PTRS_PER_PTE*sizeof(pte_t)*mm->nr_ptes) >> 10,
		swap << (PAGE_SHIFT-10));
}

unsigned long task_vsize(struct mm_struct *mm)
{
	return PAGE_SIZE * mm->total_vm;
}

unsigned long task_statm(struct mm_struct *mm,
			 unsigned long *shared, unsigned long *text,
			 unsigned long *data, unsigned long *resident)
{
	*shared = get_mm_counter(mm, MM_FILEPAGES);
	*text = (PAGE_ALIGN(mm->end_code) - (mm->start_code & PAGE_MASK))
								>> PAGE_SHIFT;
	*data = mm->total_vm - mm->shared_vm;
	*resident = *shared + get_mm_counter(mm, MM_ANONPAGES);
	return mm->total_vm;
}

static void pad_len_spaces(struct seq_file *m, int len)
{
	len = 25 + sizeof(void*) * 6 - len;
	if (len < 1)
		len = 1;
	seq_printf(m, "%*c", len, ' ');
}

#ifdef CONFIG_NUMA
/*
 * These functions are for numa_maps but called in generic **maps seq_file
 * ->start(), ->stop() ops.
 *
 * numa_maps scans all vmas under mmap_sem and checks their mempolicy.
 * Each mempolicy object is controlled by reference counting. The problem here
 * is how to avoid accessing dead mempolicy object.
 *
 * Because we're holding mmap_sem while reading seq_file, it's safe to access
 * each vma's mempolicy, no vma objects will never drop refs to mempolicy.
 *
 * A task's mempolicy (task->mempolicy) has different behavior. task->mempolicy
 * is set and replaced under mmap_sem but unrefed and cleared under task_lock().
 * So, without task_lock(), we cannot trust get_vma_policy() because we cannot
 * gurantee the task never exits under us. But taking task_lock() around
 * get_vma_plicy() causes lock order problem.
 *
 * To access task->mempolicy without lock, we hold a reference count of an
 * object pointed by task->mempolicy and remember it. This will guarantee
 * that task->mempolicy points to an alive object or NULL in numa_maps accesses.
 */
static void hold_task_mempolicy(struct proc_maps_private *priv)
{
	struct task_struct *task = priv->task;

	task_lock(task);
	priv->task_mempolicy = task->mempolicy;
	mpol_get(priv->task_mempolicy);
	task_unlock(task);
}
static void release_task_mempolicy(struct proc_maps_private *priv)
{
	mpol_put(priv->task_mempolicy);
}
#else
static void hold_task_mempolicy(struct proc_maps_private *priv)
{
}
static void release_task_mempolicy(struct proc_maps_private *priv)
{
}
#endif

static void vma_stop(struct proc_maps_private *priv, struct vm_area_struct *vma)
{
	if (vma && vma != priv->tail_vma) {
		struct mm_struct *mm = vma->vm_mm;
		release_task_mempolicy(priv);
		up_read(&mm->mmap_sem);
		mmput(mm);
	}
}

static void *m_start(struct seq_file *m, loff_t *pos)
{
	struct proc_maps_private *priv = m->private;
	unsigned long last_addr = m->version;
	struct mm_struct *mm;
	struct vm_area_struct *vma, *tail_vma = NULL;
	loff_t l = *pos;

	/* Clear the per syscall fields in priv */
	priv->task = NULL;
	priv->tail_vma = NULL;

	/*
	 * We remember last_addr rather than next_addr to hit with
	 * mmap_cache most of the time. We have zero last_addr at
	 * the beginning and also after lseek. We will have -1 last_addr
	 * after the end of the vmas.
	 */

	if (last_addr == -1UL)
		return NULL;

	priv->task = get_pid_task(priv->pid, PIDTYPE_PID);
	if (!priv->task)
		return ERR_PTR(-ESRCH);

	mm = mm_access(priv->task, PTRACE_MODE_READ_FSCREDS);
	if (!mm || IS_ERR(mm))
		return mm;
	down_read(&mm->mmap_sem);

	tail_vma = get_gate_vma(priv->task->mm);
	priv->tail_vma = tail_vma;
	hold_task_mempolicy(priv);
	/* Start with last addr hint */
	vma = find_vma(mm, last_addr);
	if (last_addr && vma) {
		vma = vma->vm_next;
		goto out;
	}

	/*
	 * Check the vma index is within the range and do
	 * sequential scan until m_index.
	 */
	vma = NULL;
	if ((unsigned long)l < mm->map_count) {
		vma = mm->mmap;
		while (l-- && vma)
			vma = vma->vm_next;
		goto out;
	}

	if (l != mm->map_count)
		tail_vma = NULL; /* After gate vma */

out:
	if (vma)
		return vma;

	release_task_mempolicy(priv);
	/* End of vmas has been reached */
	m->version = (tail_vma != NULL)? 0: -1UL;
	up_read(&mm->mmap_sem);
	mmput(mm);
	return tail_vma;
}

static void *m_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct proc_maps_private *priv = m->private;
	struct vm_area_struct *vma = v;
	struct vm_area_struct *tail_vma = priv->tail_vma;

	(*pos)++;
	if (vma && (vma != tail_vma) && vma->vm_next)
		return vma->vm_next;
	vma_stop(priv, vma);
	return (vma != tail_vma)? tail_vma: NULL;
}

static void m_stop(struct seq_file *m, void *v)
{
	struct proc_maps_private *priv = m->private;
	struct vm_area_struct *vma = v;

	if (!IS_ERR(vma))
		vma_stop(priv, vma);
	if (priv->task)
		put_task_struct(priv->task);
}

static int do_maps_open(struct inode *inode, struct file *file,
			const struct seq_operations *ops)
{
	struct proc_maps_private *priv;
	int ret = -ENOMEM;
	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (priv) {
		priv->pid = proc_pid(inode);
		ret = seq_open(file, ops);
		if (!ret) {
			struct seq_file *m = file->private_data;
			m->private = priv;
		} else {
			kfree(priv);
		}
	}
	return ret;
}

static void
show_map_vma(struct seq_file *m, struct vm_area_struct *vma, int is_pid)
{
	struct mm_struct *mm = vma->vm_mm;
	struct file *file = vma->vm_file;
	struct proc_maps_private *priv = m->private;
	struct task_struct *task = priv->task;
	vm_flags_t flags = vma->vm_flags;
	unsigned long ino = 0;
	unsigned long long pgoff = 0;
	unsigned long start, end;
	dev_t dev = 0;
	int len;
	const char *name = NULL;

	if (file) {
		struct inode *inode = file_inode(vma->vm_file);
		dev = inode->i_sb->s_dev;
		ino = inode->i_ino;
		pgoff = ((loff_t)vma->vm_pgoff) << PAGE_SHIFT;
	}

	/* We don't show the stack guard page in /proc/maps */
	start = vma->vm_start;
	if (stack_guard_page_start(vma, start))
		start += PAGE_SIZE;
	end = vma->vm_end;
	if (stack_guard_page_end(vma, end))
		end -= PAGE_SIZE;

	seq_printf(m, "%08lx-%08lx %c%c%c%c %08llx %02x:%02x %lu %n",
			start,
			end,
			flags & VM_READ ? 'r' : '-',
			flags & VM_WRITE ? 'w' : '-',
			flags & VM_EXEC ? 'x' : '-',
			flags & VM_MAYSHARE ? 's' : 'p',
			pgoff,
			MAJOR(dev), MINOR(dev), ino, &len);

	/*
	 * Print the dentry name for named mappings, and a
	 * special [heap] marker for the heap:
	 */
	if (file) {
		pad_len_spaces(m, len);
		seq_path(m, &file->f_path, "\n");
		goto done;
	}

	name = arch_vma_name(vma);
	if (!name) {
		pid_t tid;

		if (!mm) {
			name = "[vdso]";
			goto done;
		}

		if (vma->vm_start <= mm->brk &&
		    vma->vm_end >= mm->start_brk) {
			name = "[heap]";
			goto done;
		}

		tid = vm_is_stack(task, vma, is_pid);

		if (tid != 0) {
			/*
			 * Thread stack in /proc/PID/task/TID/maps or
			 * the main process stack.
			 */
			if (!is_pid || (vma->vm_start <= mm->start_stack &&
			    vma->vm_end >= mm->start_stack)) {
				name = "[stack]";
			} else {
				/* Thread stack in /proc/PID/maps */
				pad_len_spaces(m, len);
				seq_printf(m, "[stack:%d]", tid);
			}
		}
	}

done:
	if (name) {
		pad_len_spaces(m, len);
		seq_puts(m, name);
	}
	seq_putc(m, '\n');
}

static int show_map(struct seq_file *m, void *v, int is_pid)
{
	struct vm_area_struct *vma = v;
	struct proc_maps_private *priv = m->private;
	struct task_struct *task = priv->task;

	show_map_vma(m, vma, is_pid);

	if (m->count < m->size)  /* vma is copied successfully */
		m->version = (vma != get_gate_vma(task->mm))
			? vma->vm_start : 0;
	return 0;
}

static int show_pid_map(struct seq_file *m, void *v)
{
	return show_map(m, v, 1);
}

static int show_tid_map(struct seq_file *m, void *v)
{
	return show_map(m, v, 0);
}

static const struct seq_operations proc_pid_maps_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= show_pid_map
};

static const struct seq_operations proc_tid_maps_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= show_tid_map
};

static int pid_maps_open(struct inode *inode, struct file *file)
{
	return do_maps_open(inode, file, &proc_pid_maps_op);
}

static int tid_maps_open(struct inode *inode, struct file *file)
{
	return do_maps_open(inode, file, &proc_tid_maps_op);
}

const struct file_operations proc_pid_maps_operations = {
	.open		= pid_maps_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release_private,
};

const struct file_operations proc_tid_maps_operations = {
	.open		= tid_maps_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release_private,
};

/*
 * Proportional Set Size(PSS): my share of RSS.
 *
 * PSS of a process is the count of pages it has in memory, where each
 * page is divided by the number of processes sharing it.  So if a
 * process has 1000 pages all to itself, and 1000 shared with one other
 * process, its PSS will be 1500.
 *
 * To keep (accumulated) division errors low, we adopt a 64bit
 * fixed-point pss counter to minimize division errors. So (pss >>
 * PSS_SHIFT) would be the real byte count.
 *
 * A shift of 12 before division means (assuming 4K page size):
 * 	- 1M 3-user-pages add up to 8KB errors;
 * 	- supports mapcount up to 2^24, or 16M;
 * 	- supports PSS up to 2^52 bytes, or 4PB.
 */
#define PSS_SHIFT 12

#ifdef CONFIG_PROC_PAGE_MONITOR
struct mem_size_stats {
	struct vm_area_struct *vma;
	unsigned long resident;
	unsigned long shared_clean;
	unsigned long shared_dirty;
	unsigned long private_clean;
	unsigned long private_dirty;
	unsigned long referenced;
	unsigned long anonymous;
	unsigned long anonymous_thp;
	unsigned long swap;
	unsigned long nonlinear;
	u64 pss;
};


static void smaps_pte_entry(pte_t ptent, unsigned long addr,
		unsigned long ptent_size, struct mm_walk *walk)
{
	struct mem_size_stats *mss = walk->private;
	struct vm_area_struct *vma = mss->vma;
	pgoff_t pgoff = linear_page_index(vma, addr);
	struct page *page = NULL;
	int mapcount;

	if (pte_present(ptent)) {
		page = vm_normal_page(vma, addr, ptent);
	} else if (is_swap_pte(ptent)) {
		swp_entry_t swpent = pte_to_swp_entry(ptent);

		if (!non_swap_entry(swpent))
			mss->swap += ptent_size;
		else if (is_migration_entry(swpent))
			page = migration_entry_to_page(swpent);
	} else if (pte_file(ptent)) {
		if (pte_to_pgoff(ptent) != pgoff)
			mss->nonlinear += ptent_size;
	}

	if (!page)
		return;

	if (PageAnon(page))
		mss->anonymous += ptent_size;

	if (page->index != pgoff)
		mss->nonlinear += ptent_size;

	mss->resident += ptent_size;
	/* Accumulate the size in pages that have been accessed. */
	if (pte_young(ptent) || PageReferenced(page))
		mss->referenced += ptent_size;
	mapcount = page_mapcount(page);
	if (mapcount >= 2) {
		if (pte_dirty(ptent) || PageDirty(page))
			mss->shared_dirty += ptent_size;
		else
			mss->shared_clean += ptent_size;
		mss->pss += (ptent_size << PSS_SHIFT) / mapcount;
	} else {
		if (pte_dirty(ptent) || PageDirty(page))
			mss->private_dirty += ptent_size;
		else
			mss->private_clean += ptent_size;
		mss->pss += (ptent_size << PSS_SHIFT);
	}
}

static int smaps_pte_range(pmd_t *pmd, unsigned long addr, unsigned long end,
			   struct mm_walk *walk)
{
	struct mem_size_stats *mss = walk->private;
	struct vm_area_struct *vma = mss->vma;
	pte_t *pte;
	spinlock_t *ptl;

	if (pmd_trans_huge_lock(pmd, vma) == 1) {
		smaps_pte_entry(*(pte_t *)pmd, addr, HPAGE_PMD_SIZE, walk);
		spin_unlock(&walk->mm->page_table_lock);
		mss->anonymous_thp += HPAGE_PMD_SIZE;
		return 0;
	}

	if (pmd_trans_unstable(pmd))
		return 0;
	/*
	 * The mmap_sem held all the way back in m_start() is what
	 * keeps khugepaged out of here and from collapsing things
	 * in here.
	 */
	pte = pte_offset_map_lock(vma->vm_mm, pmd, addr, &ptl);
	for (; addr != end; pte++, addr += PAGE_SIZE)
		smaps_pte_entry(*pte, addr, PAGE_SIZE, walk);
	pte_unmap_unlock(pte - 1, ptl);
	cond_resched();
	return 0;
}

static void show_smap_vma_flags(struct seq_file *m, struct vm_area_struct *vma)
{
	/*
	 * Don't forget to update Documentation/ on changes.
	 */
	static const char mnemonics[BITS_PER_LONG][2] = {
		/*
		 * In case if we meet a flag we don't know about.
		 */
		[0 ... (BITS_PER_LONG-1)] = "??",

		[ilog2(VM_READ)]	= "rd",
		[ilog2(VM_WRITE)]	= "wr",
		[ilog2(VM_EXEC)]	= "ex",
		[ilog2(VM_SHARED)]	= "sh",
		[ilog2(VM_MAYREAD)]	= "mr",
		[ilog2(VM_MAYWRITE)]	= "mw",
		[ilog2(VM_MAYEXEC)]	= "me",
		[ilog2(VM_MAYSHARE)]	= "ms",
		[ilog2(VM_GROWSDOWN)]	= "gd",
		[ilog2(VM_PFNMAP)]	= "pf",
		[ilog2(VM_DENYWRITE)]	= "dw",
		[ilog2(VM_LOCKED)]	= "lo",
		[ilog2(VM_IO)]		= "io",
		[ilog2(VM_SEQ_READ)]	= "sr",
		[ilog2(VM_RAND_READ)]	= "rr",
		[ilog2(VM_DONTCOPY)]	= "dc",
		[ilog2(VM_DONTEXPAND)]	= "de",
		[ilog2(VM_ACCOUNT)]	= "ac",
		[ilog2(VM_NORESERVE)]	= "nr",
		[ilog2(VM_HUGETLB)]	= "ht",
		[ilog2(VM_NONLINEAR)]	= "nl",
		[ilog2(VM_ARCH_1)]	= "ar",
		[ilog2(VM_DONTDUMP)]	= "dd",
		[ilog2(VM_MIXEDMAP)]	= "mm",
		[ilog2(VM_HUGEPAGE)]	= "hg",
		[ilog2(VM_NOHUGEPAGE)]	= "nh",
		[ilog2(VM_MERGEABLE)]	= "mg",
	};
	size_t i;

	seq_puts(m, "VmFlags: ");
	for (i = 0; i < BITS_PER_LONG; i++) {
		if (vma->vm_flags & (1UL << i)) {
			seq_printf(m, "%c%c ",
				   mnemonics[i][0], mnemonics[i][1]);
		}
	}
	seq_putc(m, '\n');
}

static int show_smap(struct seq_file *m, void *v, int is_pid)
{
	struct proc_maps_private *priv = m->private;
	struct task_struct *task = priv->task;
	struct vm_area_struct *vma = v;
	struct mem_size_stats mss;
	struct mm_walk smaps_walk = {
		.pmd_entry = smaps_pte_range,
		.mm = vma->vm_mm,
		.private = &mss,
	};

	memset(&mss, 0, sizeof mss);
	mss.vma = vma;
	/* mmap_sem is held in m_start */
	if (vma->vm_mm && !is_vm_hugetlb_page(vma))
		walk_page_range(vma->vm_start, vma->vm_end, &smaps_walk);

	show_map_vma(m, vma, is_pid);

	seq_printf(m,
		   "Size:           %8lu kB\n"
		   "Rss:            %8lu kB\n"
		   "Pss:            %8lu kB\n"
		   "Shared_Clean:   %8lu kB\n"
		   "Shared_Dirty:   %8lu kB\n"
		   "Private_Clean:  %8lu kB\n"
		   "Private_Dirty:  %8lu kB\n"
		   "Referenced:     %8lu kB\n"
		   "Anonymous:      %8lu kB\n"
		   "AnonHugePages:  %8lu kB\n"
		   "Swap:           %8lu kB\n"
		   "KernelPageSize: %8lu kB\n"
		   "MMUPageSize:    %8lu kB\n"
		   "Locked:         %8lu kB\n",
		   (vma->vm_end - vma->vm_start) >> 10,
		   mss.resident >> 10,
		   (unsigned long)(mss.pss >> (10 + PSS_SHIFT)),
		   mss.shared_clean  >> 10,
		   mss.shared_dirty  >> 10,
		   mss.private_clean >> 10,
		   mss.private_dirty >> 10,
		   mss.referenced >> 10,
		   mss.anonymous >> 10,
		   mss.anonymous_thp >> 10,
		   mss.swap >> 10,
		   vma_kernel_pagesize(vma) >> 10,
		   vma_mmu_pagesize(vma) >> 10,
		   (vma->vm_flags & VM_LOCKED) ?
			(unsigned long)(mss.pss >> (10 + PSS_SHIFT)) : 0);

	if (vma->vm_flags & VM_NONLINEAR)
		seq_printf(m, "Nonlinear:      %8lu kB\n",
				mss.nonlinear >> 10);

	show_smap_vma_flags(m, vma);

	if (m->count < m->size)  /* vma is copied successfully */
		m->version = (vma != get_gate_vma(task->mm))
			? vma->vm_start : 0;
	return 0;
}

static int show_pid_smap(struct seq_file *m, void *v)
{
	return show_smap(m, v, 1);
}

static int show_tid_smap(struct seq_file *m, void *v)
{
	return show_smap(m, v, 0);
}

static const struct seq_operations proc_pid_smaps_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= show_pid_smap
};

static const struct seq_operations proc_tid_smaps_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= show_tid_smap
};

static int pid_smaps_open(struct inode *inode, struct file *file)
{
	return do_maps_open(inode, file, &proc_pid_smaps_op);
}

static int tid_smaps_open(struct inode *inode, struct file *file)
{
	return do_maps_open(inode, file, &proc_tid_smaps_op);
}

const struct file_operations proc_pid_smaps_operations = {
	.open		= pid_smaps_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release_private,
};

const struct file_operations proc_tid_smaps_operations = {
	.open		= tid_smaps_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release_private,
};

static int clear_refs_pte_range(pmd_t *pmd, unsigned long addr,
				unsigned long end, struct mm_walk *walk)
{
	struct vm_area_struct *vma = walk->private;
	pte_t *pte, ptent;
	spinlock_t *ptl;
	struct page *page;

	split_huge_page_pmd(vma, addr, pmd);
	if (pmd_trans_unstable(pmd))
		return 0;

	pte = pte_offset_map_lock(vma->vm_mm, pmd, addr, &ptl);
	for (; addr != end; pte++, addr += PAGE_SIZE) {
		ptent = *pte;
		if (!pte_present(ptent))
			continue;

		page = vm_normal_page(vma, addr, ptent);
		if (!page)
			continue;

		/* Clear accessed and referenced bits. */
		ptep_test_and_clear_young(vma, addr, pte);
		ClearPageReferenced(page);
	}
	pte_unmap_unlock(pte - 1, ptl);
	cond_resched();
	return 0;
}

#define CLEAR_REFS_ALL 1
#define CLEAR_REFS_ANON 2
#define CLEAR_REFS_MAPPED 3

static ssize_t clear_refs_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	struct task_struct *task;
	char buffer[PROC_NUMBUF];
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	int type;
	int rv;

	memset(buffer, 0, sizeof(buffer));
	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;
	if (copy_from_user(buffer, buf, count))
		return -EFAULT;
	rv = kstrtoint(strstrip(buffer), 10, &type);
	if (rv < 0)
		return rv;
	if (type < CLEAR_REFS_ALL || type > CLEAR_REFS_MAPPED)
		return -EINVAL;
	task = get_proc_task(file_inode(file));
	if (!task)
		return -ESRCH;
	mm = get_task_mm(task);
	if (mm) {
		struct mm_walk clear_refs_walk = {
			.pmd_entry = clear_refs_pte_range,
			.mm = mm,
		};
		down_read(&mm->mmap_sem);
		for (vma = mm->mmap; vma; vma = vma->vm_next) {
			clear_refs_walk.private = vma;
			if (is_vm_hugetlb_page(vma))
				continue;
			/*
			 * Writing 1 to /proc/pid/clear_refs affects all pages.
			 *
			 * Writing 2 to /proc/pid/clear_refs only affects
			 * Anonymous pages.
			 *
			 * Writing 3 to /proc/pid/clear_refs only affects file
			 * mapped pages.
			 */
			if (type == CLEAR_REFS_ANON && vma->vm_file)
				continue;
			if (type == CLEAR_REFS_MAPPED && !vma->vm_file)
				continue;
			walk_page_range(vma->vm_start, vma->vm_end,
					&clear_refs_walk);
		}
		flush_tlb_mm(mm);
		up_read(&mm->mmap_sem);
		mmput(mm);
	}
	put_task_struct(task);

	return count;
}

const struct file_operations proc_clear_refs_operations = {
	.write		= clear_refs_write,
	.llseek		= noop_llseek,
};

typedef struct {
	u64 pme;
} pagemap_entry_t;

struct pagemapread {
	int pos, len;		/* units: PM_ENTRY_BYTES, not bytes */
	pagemap_entry_t *buffer;
};

#define PAGEMAP_WALK_SIZE	(PMD_SIZE)
#define PAGEMAP_WALK_MASK	(PMD_MASK)

#define PM_ENTRY_BYTES      sizeof(pagemap_entry_t)
#define PM_STATUS_BITS      3
#define PM_STATUS_OFFSET    (64 - PM_STATUS_BITS)
#define PM_STATUS_MASK      (((1LL << PM_STATUS_BITS) - 1) << PM_STATUS_OFFSET)
#define PM_STATUS(nr)       (((nr) << PM_STATUS_OFFSET) & PM_STATUS_MASK)
#define PM_PSHIFT_BITS      6
#define PM_PSHIFT_OFFSET    (PM_STATUS_OFFSET - PM_PSHIFT_BITS)
#define PM_PSHIFT_MASK      (((1LL << PM_PSHIFT_BITS) - 1) << PM_PSHIFT_OFFSET)
#define PM_PSHIFT(x)        (((u64) (x) << PM_PSHIFT_OFFSET) & PM_PSHIFT_MASK)
#define PM_PFRAME_MASK      ((1LL << PM_PSHIFT_OFFSET) - 1)
#define PM_PFRAME(x)        ((x) & PM_PFRAME_MASK)

#define PM_PRESENT          PM_STATUS(4LL)
#define PM_SWAP             PM_STATUS(2LL)
#define PM_FILE             PM_STATUS(1LL)
#define PM_NOT_PRESENT      PM_PSHIFT(PAGE_SHIFT)
#define PM_END_OF_BUFFER    1

static inline pagemap_entry_t make_pme(u64 val)
{
	return (pagemap_entry_t) { .pme = val };
}

static int add_to_pagemap(unsigned long addr, pagemap_entry_t *pme,
			  struct pagemapread *pm)
{
	pm->buffer[pm->pos++] = *pme;
	if (pm->pos >= pm->len)
		return PM_END_OF_BUFFER;
	return 0;
}

static int pagemap_pte_hole(unsigned long start, unsigned long end,
				struct mm_walk *walk)
{
	struct pagemapread *pm = walk->private;
	unsigned long addr;
	int err = 0;
	pagemap_entry_t pme = make_pme(PM_NOT_PRESENT);

	for (addr = start; addr < end; addr += PAGE_SIZE) {
		err = add_to_pagemap(addr, &pme, pm);
		if (err)
			break;
	}
	return err;
}

static void pte_to_pagemap_entry(pagemap_entry_t *pme,
		struct vm_area_struct *vma, unsigned long addr, pte_t pte)
{
	u64 frame, flags;
	struct page *page = NULL;

	if (pte_present(pte)) {
		frame = pte_pfn(pte);
		flags = PM_PRESENT;
		page = vm_normal_page(vma, addr, pte);
	} else if (is_swap_pte(pte)) {
		swp_entry_t entry = pte_to_swp_entry(pte);

		frame = swp_type(entry) |
			(swp_offset(entry) << MAX_SWAPFILES_SHIFT);
		flags = PM_SWAP;
		if (is_migration_entry(entry))
			page = migration_entry_to_page(entry);
	} else {
		*pme = make_pme(PM_NOT_PRESENT);
		return;
	}

	if (page && !PageAnon(page))
		flags |= PM_FILE;

	*pme = make_pme(PM_PFRAME(frame) | PM_PSHIFT(PAGE_SHIFT) | flags);
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static void thp_pmd_to_pagemap_entry(pagemap_entry_t *pme,
					pmd_t pmd, int offset)
{
	/*
	 * Currently pmd for thp is always present because thp can not be
	 * swapped-out, migrated, or HWPOISONed (split in such cases instead.)
	 * This if-check is just to prepare for future implementation.
	 */
	if (pmd_present(pmd))
		*pme = make_pme(PM_PFRAME(pmd_pfn(pmd) + offset)
				| PM_PSHIFT(PAGE_SHIFT) | PM_PRESENT);
	else
		*pme = make_pme(PM_NOT_PRESENT);
}
#else
static inline void thp_pmd_to_pagemap_entry(pagemap_entry_t *pme,
						pmd_t pmd, int offset)
{
}
#endif

static int pagemap_pte_range(pmd_t *pmd, unsigned long addr, unsigned long end,
			     struct mm_walk *walk)
{
	struct vm_area_struct *vma;
	struct pagemapread *pm = walk->private;
	pte_t *pte;
	int err = 0;
	pagemap_entry_t pme = make_pme(PM_NOT_PRESENT);

	/* find the first VMA at or above 'addr' */
	vma = find_vma(walk->mm, addr);
	if (vma && pmd_trans_huge_lock(pmd, vma) == 1) {
		for (; addr != end; addr += PAGE_SIZE) {
			unsigned long offset;

			offset = (addr & ~PAGEMAP_WALK_MASK) >>
					PAGE_SHIFT;
			thp_pmd_to_pagemap_entry(&pme, *pmd, offset);
			err = add_to_pagemap(addr, &pme, pm);
			if (err)
				break;
		}
		spin_unlock(&walk->mm->page_table_lock);
		return err;
	}

	if (pmd_trans_unstable(pmd))
		return 0;
	for (; addr != end; addr += PAGE_SIZE) {

		/* check to see if we've left 'vma' behind
		 * and need a new, higher one */
		if (vma && (addr >= vma->vm_end)) {
			vma = find_vma(walk->mm, addr);
			pme = make_pme(PM_NOT_PRESENT);
		}

		/* check that 'vma' actually covers this address,
		 * and that it isn't a huge page vma */
		if (vma && (vma->vm_start <= addr) &&
		    !is_vm_hugetlb_page(vma)) {
			pte = pte_offset_map(pmd, addr);
			pte_to_pagemap_entry(&pme, vma, addr, *pte);
			/* unmap before userspace copy */
			pte_unmap(pte);
		}
		err = add_to_pagemap(addr, &pme, pm);
		if (err)
			return err;
	}

	cond_resched();

	return err;
}

#ifdef CONFIG_HUGETLB_PAGE
static void huge_pte_to_pagemap_entry(pagemap_entry_t *pme,
					pte_t pte, int offset)
{
	if (pte_present(pte))
		*pme = make_pme(PM_PFRAME(pte_pfn(pte) + offset)
				| PM_PSHIFT(PAGE_SHIFT) | PM_PRESENT);
	else
		*pme = make_pme(PM_NOT_PRESENT);
}

/* This function walks within one hugetlb entry in the single call */
static int pagemap_hugetlb_range(pte_t *pte, unsigned long hmask,
				 unsigned long addr, unsigned long end,
				 struct mm_walk *walk)
{
	struct pagemapread *pm = walk->private;
	int err = 0;
	pagemap_entry_t pme;

	for (; addr != end; addr += PAGE_SIZE) {
		int offset = (addr & ~hmask) >> PAGE_SHIFT;
		huge_pte_to_pagemap_entry(&pme, *pte, offset);
		err = add_to_pagemap(addr, &pme, pm);
		if (err)
			return err;
	}

	cond_resched();

	return err;
}
#endif /* HUGETLB_PAGE */

/*
 * /proc/pid/pagemap - an array mapping virtual pages to pfns
 *
 * For each page in the address space, this file contains one 64-bit entry
 * consisting of the following:
 *
 * Bits 0-54  page frame number (PFN) if present
 * Bits 0-4   swap type if swapped
 * Bits 5-54  swap offset if swapped
 * Bits 55-60 page shift (page size = 1<<page shift)
 * Bit  61    page is file-page or shared-anon
 * Bit  62    page swapped
 * Bit  63    page present
 *
 * If the page is not present but in swap, then the PFN contains an
 * encoding of the swap file number and the page's offset into the
 * swap. Unmapped pages return a null PFN. This allows determining
 * precisely which pages are mapped (or in swap) and comparing mapped
 * pages between processes.
 *
 * Efficient users of this interface will use /proc/pid/maps to
 * determine which areas of memory are actually mapped and llseek to
 * skip over unmapped regions.
 */
static ssize_t pagemap_read(struct file *file, char __user *buf,
			    size_t count, loff_t *ppos)
{
	struct task_struct *task = get_proc_task(file_inode(file));
	struct mm_struct *mm;
	struct pagemapread pm;
	int ret = -ESRCH;
	struct mm_walk pagemap_walk = {};
	unsigned long src;
	unsigned long svpfn;
	unsigned long start_vaddr;
	unsigned long end_vaddr;
	int copied = 0;

	if (!task)
		goto out;

	ret = -EINVAL;
	/* file position must be aligned */
	if ((*ppos % PM_ENTRY_BYTES) || (count % PM_ENTRY_BYTES))
		goto out_task;

	ret = 0;
	if (!count)
		goto out_task;

	pm.len = (PAGEMAP_WALK_SIZE >> PAGE_SHIFT);
	pm.buffer = kmalloc(pm.len * PM_ENTRY_BYTES, GFP_TEMPORARY);
	ret = -ENOMEM;
	if (!pm.buffer)
		goto out_task;

	mm = mm_access(task, PTRACE_MODE_READ_FSCREDS);
	ret = PTR_ERR(mm);
	if (!mm || IS_ERR(mm))
		goto out_free;

	pagemap_walk.pmd_entry = pagemap_pte_range;
	pagemap_walk.pte_hole = pagemap_pte_hole;
#ifdef CONFIG_HUGETLB_PAGE
	pagemap_walk.hugetlb_entry = pagemap_hugetlb_range;
#endif
	pagemap_walk.mm = mm;
	pagemap_walk.private = &pm;

	src = *ppos;
	svpfn = src / PM_ENTRY_BYTES;
	start_vaddr = svpfn << PAGE_SHIFT;
	end_vaddr = TASK_SIZE_OF(task);

	/* watch out for wraparound */
	if (svpfn > TASK_SIZE_OF(task) >> PAGE_SHIFT)
		start_vaddr = end_vaddr;

	/*
	 * The odds are that this will stop walking way
	 * before end_vaddr, because the length of the
	 * user buffer is tracked in "pm", and the walk
	 * will stop when we hit the end of the buffer.
	 */
	ret = 0;
	while (count && (start_vaddr < end_vaddr)) {
		int len;
		unsigned long end;

		pm.pos = 0;
		end = (start_vaddr + PAGEMAP_WALK_SIZE) & PAGEMAP_WALK_MASK;
		/* overflow ? */
		if (end < start_vaddr || end > end_vaddr)
			end = end_vaddr;
		down_read(&mm->mmap_sem);
		ret = walk_page_range(start_vaddr, end, &pagemap_walk);
		up_read(&mm->mmap_sem);
		start_vaddr = end;

		len = min(count, PM_ENTRY_BYTES * pm.pos);
		if (copy_to_user(buf, pm.buffer, len)) {
			ret = -EFAULT;
			goto out_mm;
		}
		copied += len;
		buf += len;
		count -= len;
	}
	*ppos += copied;
	if (!ret || ret == PM_END_OF_BUFFER)
		ret = copied;

out_mm:
	mmput(mm);
out_free:
	kfree(pm.buffer);
out_task:
	put_task_struct(task);
out:
	return ret;
}

static int pagemap_open(struct inode *inode, struct file *file)
{
	/* do not disclose physical addresses to unprivileged
	   userspace (closes a rowhammer attack vector) */
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	return 0;
}

const struct file_operations proc_pagemap_operations = {
	.llseek		= mem_lseek, /* borrow this */
	.read		= pagemap_read,
	.open		= pagemap_open,
};
#endif /* CONFIG_PROC_PAGE_MONITOR */

#ifdef CONFIG_NUMA

struct numa_maps {
	struct vm_area_struct *vma;
	unsigned long pages;
	unsigned long anon;
	unsigned long active;
	unsigned long writeback;
	unsigned long mapcount_max;
	unsigned long dirty;
	unsigned long swapcache;
	unsigned long node[MAX_NUMNODES];
};

struct numa_maps_private {
	struct proc_maps_private proc_maps;
	struct numa_maps md;
};

static void gather_stats(struct page *page, struct numa_maps *md, int pte_dirty,
			unsigned long nr_pages)
{
	int count = page_mapcount(page);

	md->pages += nr_pages;
	if (pte_dirty || PageDirty(page))
		md->dirty += nr_pages;

	if (PageSwapCache(page))
		md->swapcache += nr_pages;

	if (PageActive(page) || PageUnevictable(page))
		md->active += nr_pages;

	if (PageWriteback(page))
		md->writeback += nr_pages;

	if (PageAnon(page))
		md->anon += nr_pages;

	if (count > md->mapcount_max)
		md->mapcount_max = count;

	md->node[page_to_nid(page)] += nr_pages;
}

static struct page *can_gather_numa_stats(pte_t pte, struct vm_area_struct *vma,
		unsigned long addr)
{
	struct page *page;
	int nid;

	if (!pte_present(pte))
		return NULL;

	page = vm_normal_page(vma, addr, pte);
	if (!page)
		return NULL;

	if (PageReserved(page))
		return NULL;

	nid = page_to_nid(page);
	if (!node_isset(nid, node_states[N_MEMORY]))
		return NULL;

	return page;
}

static int gather_pte_stats(pmd_t *pmd, unsigned long addr,
		unsigned long end, struct mm_walk *walk)
{
	struct numa_maps *md;
	spinlock_t *ptl;
	pte_t *orig_pte;
	pte_t *pte;

	md = walk->private;

	if (pmd_trans_huge_lock(pmd, md->vma) == 1) {
		pte_t huge_pte = *(pte_t *)pmd;
		struct page *page;

		page = can_gather_numa_stats(huge_pte, md->vma, addr);
		if (page)
			gather_stats(page, md, pte_dirty(huge_pte),
				     HPAGE_PMD_SIZE/PAGE_SIZE);
		spin_unlock(&walk->mm->page_table_lock);
		return 0;
	}

	if (pmd_trans_unstable(pmd))
		return 0;
	orig_pte = pte = pte_offset_map_lock(walk->mm, pmd, addr, &ptl);
	do {
		struct page *page = can_gather_numa_stats(*pte, md->vma, addr);
		if (!page)
			continue;
		gather_stats(page, md, pte_dirty(*pte), 1);

	} while (pte++, addr += PAGE_SIZE, addr != end);
	pte_unmap_unlock(orig_pte, ptl);
	return 0;
}
#ifdef CONFIG_HUGETLB_PAGE
static int gather_hugetbl_stats(pte_t *pte, unsigned long hmask,
		unsigned long addr, unsigned long end, struct mm_walk *walk)
{
	struct numa_maps *md;
	struct page *page;

	if (pte_none(*pte))
		return 0;

	page = pte_page(*pte);
	if (!page)
		return 0;

	md = walk->private;
	gather_stats(page, md, pte_dirty(*pte), 1);
	return 0;
}

#else
static int gather_hugetbl_stats(pte_t *pte, unsigned long hmask,
		unsigned long addr, unsigned long end, struct mm_walk *walk)
{
	return 0;
}
#endif

/*
 * Display pages allocated per node and memory policy via /proc.
 */
static int show_numa_map(struct seq_file *m, void *v, int is_pid)
{
	struct numa_maps_private *numa_priv = m->private;
	struct proc_maps_private *proc_priv = &numa_priv->proc_maps;
	struct vm_area_struct *vma = v;
	struct numa_maps *md = &numa_priv->md;
	struct file *file = vma->vm_file;
	struct task_struct *task = proc_priv->task;
	struct mm_struct *mm = vma->vm_mm;
	struct mm_walk walk = {};
	struct mempolicy *pol;
	int n;
	char buffer[50];

	if (!mm)
		return 0;

	/* Ensure we start with an empty set of numa_maps statistics. */
	memset(md, 0, sizeof(*md));

	md->vma = vma;

	walk.hugetlb_entry = gather_hugetbl_stats;
	walk.pmd_entry = gather_pte_stats;
	walk.private = md;
	walk.mm = mm;

	pol = get_vma_policy(task, vma, vma->vm_start);
	mpol_to_str(buffer, sizeof(buffer), pol);
	mpol_cond_put(pol);

	seq_printf(m, "%08lx %s", vma->vm_start, buffer);

	if (file) {
		seq_printf(m, " file=");
		seq_path(m, &file->f_path, "\n\t= ");
	} else if (vma->vm_start <= mm->brk && vma->vm_end >= mm->start_brk) {
		seq_printf(m, " heap");
	} else {
		pid_t tid = vm_is_stack(task, vma, is_pid);
		if (tid != 0) {
			/*
			 * Thread stack in /proc/PID/task/TID/maps or
			 * the main process stack.
			 */
			if (!is_pid || (vma->vm_start <= mm->start_stack &&
			    vma->vm_end >= mm->start_stack))
				seq_printf(m, " stack");
			else
				seq_printf(m, " stack:%d", tid);
		}
	}

	if (is_vm_hugetlb_page(vma))
		seq_printf(m, " huge");

	walk_page_range(vma->vm_start, vma->vm_end, &walk);

	if (!md->pages)
		goto out;

	if (md->anon)
		seq_printf(m, " anon=%lu", md->anon);

	if (md->dirty)
		seq_printf(m, " dirty=%lu", md->dirty);

	if (md->pages != md->anon && md->pages != md->dirty)
		seq_printf(m, " mapped=%lu", md->pages);

	if (md->mapcount_max > 1)
		seq_printf(m, " mapmax=%lu", md->mapcount_max);

	if (md->swapcache)
		seq_printf(m, " swapcache=%lu", md->swapcache);

	if (md->active < md->pages && !is_vm_hugetlb_page(vma))
		seq_printf(m, " active=%lu", md->active);

	if (md->writeback)
		seq_printf(m, " writeback=%lu", md->writeback);

	for_each_node_state(n, N_MEMORY)
		if (md->node[n])
			seq_printf(m, " N%d=%lu", n, md->node[n]);
out:
	seq_putc(m, '\n');

	if (m->count < m->size)
		m->version = (vma != proc_priv->tail_vma) ? vma->vm_start : 0;
	return 0;
}

static int show_pid_numa_map(struct seq_file *m, void *v)
{
	return show_numa_map(m, v, 1);
}

static int show_tid_numa_map(struct seq_file *m, void *v)
{
	return show_numa_map(m, v, 0);
}

static const struct seq_operations proc_pid_numa_maps_op = {
	.start  = m_start,
	.next   = m_next,
	.stop   = m_stop,
	.show   = show_pid_numa_map,
};

static const struct seq_operations proc_tid_numa_maps_op = {
	.start  = m_start,
	.next   = m_next,
	.stop   = m_stop,
	.show   = show_tid_numa_map,
};

static int numa_maps_open(struct inode *inode, struct file *file,
			  const struct seq_operations *ops)
{
	struct numa_maps_private *priv;
	int ret = -ENOMEM;
	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (priv) {
		priv->proc_maps.pid = proc_pid(inode);
		ret = seq_open(file, ops);
		if (!ret) {
			struct seq_file *m = file->private_data;
			m->private = priv;
		} else {
			kfree(priv);
		}
	}
	return ret;
}

static int pid_numa_maps_open(struct inode *inode, struct file *file)
{
	return numa_maps_open(inode, file, &proc_pid_numa_maps_op);
}

static int tid_numa_maps_open(struct inode *inode, struct file *file)
{
	return numa_maps_open(inode, file, &proc_tid_numa_maps_op);
}

const struct file_operations proc_pid_numa_maps_operations = {
	.open		= pid_numa_maps_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release_private,
};

const struct file_operations proc_tid_numa_maps_operations = {
	.open		= tid_numa_maps_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release_private,
};
#endif /* CONFIG_NUMA */
