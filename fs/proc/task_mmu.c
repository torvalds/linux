#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/mount.h>
#include <linux/seq_file.h>
#include <linux/highmem.h>
#include <linux/ptrace.h>
#include <linux/pagemap.h>
#include <linux/ptrace.h>
#include <linux/mempolicy.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/seq_file.h>

#include <asm/elf.h>
#include <asm/uaccess.h>
#include <asm/tlbflush.h>
#include "internal.h"

void task_mem(struct seq_file *m, struct mm_struct *mm)
{
	unsigned long data, text, lib;
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
	seq_printf(m,
		"VmPeak:\t%8lu kB\n"
		"VmSize:\t%8lu kB\n"
		"VmLck:\t%8lu kB\n"
		"VmHWM:\t%8lu kB\n"
		"VmRSS:\t%8lu kB\n"
		"VmData:\t%8lu kB\n"
		"VmStk:\t%8lu kB\n"
		"VmExe:\t%8lu kB\n"
		"VmLib:\t%8lu kB\n"
		"VmPTE:\t%8lu kB\n",
		hiwater_vm << (PAGE_SHIFT-10),
		(total_vm - mm->reserved_vm) << (PAGE_SHIFT-10),
		mm->locked_vm << (PAGE_SHIFT-10),
		hiwater_rss << (PAGE_SHIFT-10),
		total_rss << (PAGE_SHIFT-10),
		data << (PAGE_SHIFT-10),
		mm->stack_vm << (PAGE_SHIFT-10), text, lib,
		(PTRS_PER_PTE*sizeof(pte_t)*mm->nr_ptes) >> 10);
}

unsigned long task_vsize(struct mm_struct *mm)
{
	return PAGE_SIZE * mm->total_vm;
}

int task_statm(struct mm_struct *mm, int *shared, int *text,
	       int *data, int *resident)
{
	*shared = get_mm_counter(mm, file_rss);
	*text = (PAGE_ALIGN(mm->end_code) - (mm->start_code & PAGE_MASK))
								>> PAGE_SHIFT;
	*data = mm->total_vm - mm->shared_vm;
	*resident = *shared + get_mm_counter(mm, anon_rss);
	return mm->total_vm;
}

static void pad_len_spaces(struct seq_file *m, int len)
{
	len = 25 + sizeof(void*) * 6 - len;
	if (len < 1)
		len = 1;
	seq_printf(m, "%*c", len, ' ');
}

static void vma_stop(struct proc_maps_private *priv, struct vm_area_struct *vma)
{
	if (vma && vma != priv->tail_vma) {
		struct mm_struct *mm = vma->vm_mm;
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
		return NULL;

	mm = mm_for_maps(priv->task);
	if (!mm)
		return NULL;

	tail_vma = get_gate_vma(priv->task);
	priv->tail_vma = tail_vma;

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

static int show_map(struct seq_file *m, void *v)
{
	struct proc_maps_private *priv = m->private;
	struct task_struct *task = priv->task;
	struct vm_area_struct *vma = v;
	struct mm_struct *mm = vma->vm_mm;
	struct file *file = vma->vm_file;
	int flags = vma->vm_flags;
	unsigned long ino = 0;
	dev_t dev = 0;
	int len;

	if (maps_protect && !ptrace_may_attach(task))
		return -EACCES;

	if (file) {
		struct inode *inode = vma->vm_file->f_path.dentry->d_inode;
		dev = inode->i_sb->s_dev;
		ino = inode->i_ino;
	}

	seq_printf(m, "%08lx-%08lx %c%c%c%c %08lx %02x:%02x %lu %n",
			vma->vm_start,
			vma->vm_end,
			flags & VM_READ ? 'r' : '-',
			flags & VM_WRITE ? 'w' : '-',
			flags & VM_EXEC ? 'x' : '-',
			flags & VM_MAYSHARE ? 's' : 'p',
			vma->vm_pgoff << PAGE_SHIFT,
			MAJOR(dev), MINOR(dev), ino, &len);

	/*
	 * Print the dentry name for named mappings, and a
	 * special [heap] marker for the heap:
	 */
	if (file) {
		pad_len_spaces(m, len);
		seq_path(m, &file->f_path, "\n");
	} else {
		const char *name = arch_vma_name(vma);
		if (!name) {
			if (mm) {
				if (vma->vm_start <= mm->start_brk &&
						vma->vm_end >= mm->brk) {
					name = "[heap]";
				} else if (vma->vm_start <= mm->start_stack &&
					   vma->vm_end >= mm->start_stack) {
					name = "[stack]";
				}
			} else {
				name = "[vdso]";
			}
		}
		if (name) {
			pad_len_spaces(m, len);
			seq_puts(m, name);
		}
	}
	seq_putc(m, '\n');

	if (m->count < m->size)  /* vma is copied successfully */
		m->version = (vma != get_gate_vma(task))? vma->vm_start: 0;
	return 0;
}

static const struct seq_operations proc_pid_maps_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= show_map
};

static int maps_open(struct inode *inode, struct file *file)
{
	return do_maps_open(inode, file, &proc_pid_maps_op);
}

const struct file_operations proc_maps_operations = {
	.open		= maps_open,
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
	unsigned long swap;
	u64 pss;
};

static int smaps_pte_range(pmd_t *pmd, unsigned long addr, unsigned long end,
			   void *private)
{
	struct mem_size_stats *mss = private;
	struct vm_area_struct *vma = mss->vma;
	pte_t *pte, ptent;
	spinlock_t *ptl;
	struct page *page;
	int mapcount;

	pte = pte_offset_map_lock(vma->vm_mm, pmd, addr, &ptl);
	for (; addr != end; pte++, addr += PAGE_SIZE) {
		ptent = *pte;

		if (is_swap_pte(ptent)) {
			mss->swap += PAGE_SIZE;
			continue;
		}

		if (!pte_present(ptent))
			continue;

		mss->resident += PAGE_SIZE;

		page = vm_normal_page(vma, addr, ptent);
		if (!page)
			continue;

		/* Accumulate the size in pages that have been accessed. */
		if (pte_young(ptent) || PageReferenced(page))
			mss->referenced += PAGE_SIZE;
		mapcount = page_mapcount(page);
		if (mapcount >= 2) {
			if (pte_dirty(ptent))
				mss->shared_dirty += PAGE_SIZE;
			else
				mss->shared_clean += PAGE_SIZE;
			mss->pss += (PAGE_SIZE << PSS_SHIFT) / mapcount;
		} else {
			if (pte_dirty(ptent))
				mss->private_dirty += PAGE_SIZE;
			else
				mss->private_clean += PAGE_SIZE;
			mss->pss += (PAGE_SIZE << PSS_SHIFT);
		}
	}
	pte_unmap_unlock(pte - 1, ptl);
	cond_resched();
	return 0;
}

static struct mm_walk smaps_walk = { .pmd_entry = smaps_pte_range };

static int show_smap(struct seq_file *m, void *v)
{
	struct vm_area_struct *vma = v;
	struct mem_size_stats mss;
	int ret;

	memset(&mss, 0, sizeof mss);
	mss.vma = vma;
	if (vma->vm_mm && !is_vm_hugetlb_page(vma))
		walk_page_range(vma->vm_mm, vma->vm_start, vma->vm_end,
				&smaps_walk, &mss);

	ret = show_map(m, v);
	if (ret)
		return ret;

	seq_printf(m,
		   "Size:           %8lu kB\n"
		   "Rss:            %8lu kB\n"
		   "Pss:            %8lu kB\n"
		   "Shared_Clean:   %8lu kB\n"
		   "Shared_Dirty:   %8lu kB\n"
		   "Private_Clean:  %8lu kB\n"
		   "Private_Dirty:  %8lu kB\n"
		   "Referenced:     %8lu kB\n"
		   "Swap:           %8lu kB\n",
		   (vma->vm_end - vma->vm_start) >> 10,
		   mss.resident >> 10,
		   (unsigned long)(mss.pss >> (10 + PSS_SHIFT)),
		   mss.shared_clean  >> 10,
		   mss.shared_dirty  >> 10,
		   mss.private_clean >> 10,
		   mss.private_dirty >> 10,
		   mss.referenced >> 10,
		   mss.swap >> 10);

	return ret;
}

static const struct seq_operations proc_pid_smaps_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= show_smap
};

static int smaps_open(struct inode *inode, struct file *file)
{
	return do_maps_open(inode, file, &proc_pid_smaps_op);
}

const struct file_operations proc_smaps_operations = {
	.open		= smaps_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release_private,
};

static int clear_refs_pte_range(pmd_t *pmd, unsigned long addr,
				unsigned long end, void *private)
{
	struct vm_area_struct *vma = private;
	pte_t *pte, ptent;
	spinlock_t *ptl;
	struct page *page;

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

static struct mm_walk clear_refs_walk = { .pmd_entry = clear_refs_pte_range };

static ssize_t clear_refs_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	struct task_struct *task;
	char buffer[PROC_NUMBUF], *end;
	struct mm_struct *mm;
	struct vm_area_struct *vma;

	memset(buffer, 0, sizeof(buffer));
	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;
	if (copy_from_user(buffer, buf, count))
		return -EFAULT;
	if (!simple_strtol(buffer, &end, 0))
		return -EINVAL;
	if (*end == '\n')
		end++;
	task = get_proc_task(file->f_path.dentry->d_inode);
	if (!task)
		return -ESRCH;
	mm = get_task_mm(task);
	if (mm) {
		down_read(&mm->mmap_sem);
		for (vma = mm->mmap; vma; vma = vma->vm_next)
			if (!is_vm_hugetlb_page(vma))
				walk_page_range(mm, vma->vm_start, vma->vm_end,
						&clear_refs_walk, vma);
		flush_tlb_mm(mm);
		up_read(&mm->mmap_sem);
		mmput(mm);
	}
	put_task_struct(task);
	if (end - buffer == 0)
		return -EIO;
	return end - buffer;
}

const struct file_operations proc_clear_refs_operations = {
	.write		= clear_refs_write,
};

struct pagemapread {
	char __user *out, *end;
};

#define PM_ENTRY_BYTES      sizeof(u64)
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
#define PM_NOT_PRESENT      PM_PSHIFT(PAGE_SHIFT)
#define PM_END_OF_BUFFER    1

static int add_to_pagemap(unsigned long addr, u64 pfn,
			  struct pagemapread *pm)
{
	/*
	 * Make sure there's room in the buffer for an
	 * entire entry.  Otherwise, only copy part of
	 * the pfn.
	 */
	if (pm->out + PM_ENTRY_BYTES >= pm->end) {
		if (copy_to_user(pm->out, &pfn, pm->end - pm->out))
			return -EFAULT;
		pm->out = pm->end;
		return PM_END_OF_BUFFER;
	}

	if (put_user(pfn, pm->out))
		return -EFAULT;
	pm->out += PM_ENTRY_BYTES;
	return 0;
}

static int pagemap_pte_hole(unsigned long start, unsigned long end,
				void *private)
{
	struct pagemapread *pm = private;
	unsigned long addr;
	int err = 0;
	for (addr = start; addr < end; addr += PAGE_SIZE) {
		err = add_to_pagemap(addr, PM_NOT_PRESENT, pm);
		if (err)
			break;
	}
	return err;
}

static u64 swap_pte_to_pagemap_entry(pte_t pte)
{
	swp_entry_t e = pte_to_swp_entry(pte);
	return swp_type(e) | (swp_offset(e) << MAX_SWAPFILES_SHIFT);
}

static int pagemap_pte_range(pmd_t *pmd, unsigned long addr, unsigned long end,
			     void *private)
{
	struct pagemapread *pm = private;
	pte_t *pte;
	int err = 0;

	for (; addr != end; addr += PAGE_SIZE) {
		u64 pfn = PM_NOT_PRESENT;
		pte = pte_offset_map(pmd, addr);
		if (is_swap_pte(*pte))
			pfn = PM_PFRAME(swap_pte_to_pagemap_entry(*pte))
				| PM_PSHIFT(PAGE_SHIFT) | PM_SWAP;
		else if (pte_present(*pte))
			pfn = PM_PFRAME(pte_pfn(*pte))
				| PM_PSHIFT(PAGE_SHIFT) | PM_PRESENT;
		/* unmap so we're not in atomic when we copy to userspace */
		pte_unmap(pte);
		err = add_to_pagemap(addr, pfn, pm);
		if (err)
			return err;
	}

	cond_resched();

	return err;
}

static struct mm_walk pagemap_walk = {
	.pmd_entry = pagemap_pte_range,
	.pte_hole = pagemap_pte_hole
};

/*
 * /proc/pid/pagemap - an array mapping virtual pages to pfns
 *
 * For each page in the address space, this file contains one 64-bit entry
 * consisting of the following:
 *
 * Bits 0-55  page frame number (PFN) if present
 * Bits 0-4   swap type if swapped
 * Bits 5-55  swap offset if swapped
 * Bits 55-60 page shift (page size = 1<<page shift)
 * Bit  61    reserved for future use
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
	struct task_struct *task = get_proc_task(file->f_path.dentry->d_inode);
	struct page **pages, *page;
	unsigned long uaddr, uend;
	struct mm_struct *mm;
	struct pagemapread pm;
	int pagecount;
	int ret = -ESRCH;

	if (!task)
		goto out;

	ret = -EACCES;
	if (!ptrace_may_attach(task))
		goto out_task;

	ret = -EINVAL;
	/* file position must be aligned */
	if (*ppos % PM_ENTRY_BYTES)
		goto out_task;

	ret = 0;
	mm = get_task_mm(task);
	if (!mm)
		goto out_task;

	ret = -ENOMEM;
	uaddr = (unsigned long)buf & PAGE_MASK;
	uend = (unsigned long)(buf + count);
	pagecount = (PAGE_ALIGN(uend) - uaddr) / PAGE_SIZE;
	pages = kmalloc(pagecount * sizeof(struct page *), GFP_KERNEL);
	if (!pages)
		goto out_mm;

	down_read(&current->mm->mmap_sem);
	ret = get_user_pages(current, current->mm, uaddr, pagecount,
			     1, 0, pages, NULL);
	up_read(&current->mm->mmap_sem);

	if (ret < 0)
		goto out_free;

	if (ret != pagecount) {
		pagecount = ret;
		ret = -EFAULT;
		goto out_pages;
	}

	pm.out = buf;
	pm.end = buf + count;

	if (!ptrace_may_attach(task)) {
		ret = -EIO;
	} else {
		unsigned long src = *ppos;
		unsigned long svpfn = src / PM_ENTRY_BYTES;
		unsigned long start_vaddr = svpfn << PAGE_SHIFT;
		unsigned long end_vaddr = TASK_SIZE_OF(task);

		/* watch out for wraparound */
		if (svpfn > TASK_SIZE_OF(task) >> PAGE_SHIFT)
			start_vaddr = end_vaddr;

		/*
		 * The odds are that this will stop walking way
		 * before end_vaddr, because the length of the
		 * user buffer is tracked in "pm", and the walk
		 * will stop when we hit the end of the buffer.
		 */
		ret = walk_page_range(mm, start_vaddr, end_vaddr,
					&pagemap_walk, &pm);
		if (ret == PM_END_OF_BUFFER)
			ret = 0;
		/* don't need mmap_sem for these, but this looks cleaner */
		*ppos += pm.out - buf;
		if (!ret)
			ret = pm.out - buf;
	}

out_pages:
	for (; pagecount; pagecount--) {
		page = pages[pagecount-1];
		if (!PageReserved(page))
			SetPageDirty(page);
		page_cache_release(page);
	}
out_free:
	kfree(pages);
out_mm:
	mmput(mm);
out_task:
	put_task_struct(task);
out:
	return ret;
}

const struct file_operations proc_pagemap_operations = {
	.llseek		= mem_lseek, /* borrow this */
	.read		= pagemap_read,
};
#endif /* CONFIG_PROC_PAGE_MONITOR */

#ifdef CONFIG_NUMA
extern int show_numa_map(struct seq_file *m, void *v);

static int show_numa_map_checked(struct seq_file *m, void *v)
{
	struct proc_maps_private *priv = m->private;
	struct task_struct *task = priv->task;

	if (maps_protect && !ptrace_may_attach(task))
		return -EACCES;

	return show_numa_map(m, v);
}

static const struct seq_operations proc_pid_numa_maps_op = {
        .start  = m_start,
        .next   = m_next,
        .stop   = m_stop,
        .show   = show_numa_map_checked
};

static int numa_maps_open(struct inode *inode, struct file *file)
{
	return do_maps_open(inode, file, &proc_pid_numa_maps_op);
}

const struct file_operations proc_numa_maps_operations = {
	.open		= numa_maps_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release_private,
};
#endif
