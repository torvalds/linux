#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/mount.h>
#include <linux/seq_file.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/mempolicy.h>

#include <asm/elf.h>
#include <asm/uaccess.h>
#include <asm/tlbflush.h>
#include "internal.h"

char *task_mem(struct mm_struct *mm, char *buffer)
{
	unsigned long data, text, lib;

	data = mm->total_vm - mm->shared_vm - mm->stack_vm;
	text = (PAGE_ALIGN(mm->end_code) - (mm->start_code & PAGE_MASK)) >> 10;
	lib = (mm->exec_vm << (PAGE_SHIFT-10)) - text;
	buffer += sprintf(buffer,
		"VmSize:\t%8lu kB\n"
		"VmLck:\t%8lu kB\n"
		"VmRSS:\t%8lu kB\n"
		"VmData:\t%8lu kB\n"
		"VmStk:\t%8lu kB\n"
		"VmExe:\t%8lu kB\n"
		"VmLib:\t%8lu kB\n"
		"VmPTE:\t%8lu kB\n",
		(mm->total_vm - mm->reserved_vm) << (PAGE_SHIFT-10),
		mm->locked_vm << (PAGE_SHIFT-10),
		get_mm_counter(mm, rss) << (PAGE_SHIFT-10),
		data << (PAGE_SHIFT-10),
		mm->stack_vm << (PAGE_SHIFT-10), text, lib,
		(PTRS_PER_PTE*sizeof(pte_t)*mm->nr_ptes) >> 10);
	return buffer;
}

unsigned long task_vsize(struct mm_struct *mm)
{
	return PAGE_SIZE * mm->total_vm;
}

int task_statm(struct mm_struct *mm, int *shared, int *text,
	       int *data, int *resident)
{
	int rss = get_mm_counter(mm, rss);

	*shared = rss - get_mm_counter(mm, anon_rss);
	*text = (PAGE_ALIGN(mm->end_code) - (mm->start_code & PAGE_MASK))
								>> PAGE_SHIFT;
	*data = mm->total_vm - mm->shared_vm;
	*resident = rss;
	return mm->total_vm;
}

int proc_exe_link(struct inode *inode, struct dentry **dentry, struct vfsmount **mnt)
{
	struct vm_area_struct * vma;
	int result = -ENOENT;
	struct task_struct *task = proc_task(inode);
	struct mm_struct * mm = get_task_mm(task);

	if (!mm)
		goto out;
	down_read(&mm->mmap_sem);

	vma = mm->mmap;
	while (vma) {
		if ((vma->vm_flags & VM_EXECUTABLE) && vma->vm_file)
			break;
		vma = vma->vm_next;
	}

	if (vma) {
		*mnt = mntget(vma->vm_file->f_vfsmnt);
		*dentry = dget(vma->vm_file->f_dentry);
		result = 0;
	}

	up_read(&mm->mmap_sem);
	mmput(mm);
out:
	return result;
}

static void pad_len_spaces(struct seq_file *m, int len)
{
	len = 25 + sizeof(void*) * 6 - len;
	if (len < 1)
		len = 1;
	seq_printf(m, "%*c", len, ' ');
}

struct mem_size_stats
{
	unsigned long resident;
	unsigned long shared_clean;
	unsigned long shared_dirty;
	unsigned long private_clean;
	unsigned long private_dirty;
};

static int show_map_internal(struct seq_file *m, void *v, struct mem_size_stats *mss)
{
	struct task_struct *task = m->private;
	struct vm_area_struct *vma = v;
	struct mm_struct *mm = vma->vm_mm;
	struct file *file = vma->vm_file;
	int flags = vma->vm_flags;
	unsigned long ino = 0;
	dev_t dev = 0;
	int len;

	if (file) {
		struct inode *inode = vma->vm_file->f_dentry->d_inode;
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
		seq_path(m, file->f_vfsmnt, file->f_dentry, "\n");
	} else {
		if (mm) {
			if (vma->vm_start <= mm->start_brk &&
						vma->vm_end >= mm->brk) {
				pad_len_spaces(m, len);
				seq_puts(m, "[heap]");
			} else {
				if (vma->vm_start <= mm->start_stack &&
					vma->vm_end >= mm->start_stack) {

					pad_len_spaces(m, len);
					seq_puts(m, "[stack]");
				}
			}
		} else {
			pad_len_spaces(m, len);
			seq_puts(m, "[vdso]");
		}
	}
	seq_putc(m, '\n');

	if (mss)
		seq_printf(m,
			   "Size:          %8lu kB\n"
			   "Rss:           %8lu kB\n"
			   "Shared_Clean:  %8lu kB\n"
			   "Shared_Dirty:  %8lu kB\n"
			   "Private_Clean: %8lu kB\n"
			   "Private_Dirty: %8lu kB\n",
			   (vma->vm_end - vma->vm_start) >> 10,
			   mss->resident >> 10,
			   mss->shared_clean  >> 10,
			   mss->shared_dirty  >> 10,
			   mss->private_clean >> 10,
			   mss->private_dirty >> 10);

	if (m->count < m->size)  /* vma is copied successfully */
		m->version = (vma != get_gate_vma(task))? vma->vm_start: 0;
	return 0;
}

static int show_map(struct seq_file *m, void *v)
{
	return show_map_internal(m, v, 0);
}

static void smaps_pte_range(struct vm_area_struct *vma, pmd_t *pmd,
				unsigned long addr, unsigned long end,
				struct mem_size_stats *mss)
{
	pte_t *pte, ptent;
	unsigned long pfn;
	struct page *page;

	pte = pte_offset_map(pmd, addr);
	do {
		ptent = *pte;
		if (pte_none(ptent) || !pte_present(ptent))
			continue;

		mss->resident += PAGE_SIZE;
		pfn = pte_pfn(ptent);
		if (!pfn_valid(pfn))
			continue;

		page = pfn_to_page(pfn);
		if (page_count(page) >= 2) {
			if (pte_dirty(ptent))
				mss->shared_dirty += PAGE_SIZE;
			else
				mss->shared_clean += PAGE_SIZE;
		} else {
			if (pte_dirty(ptent))
				mss->private_dirty += PAGE_SIZE;
			else
				mss->private_clean += PAGE_SIZE;
		}
	} while (pte++, addr += PAGE_SIZE, addr != end);
	pte_unmap(pte - 1);
	cond_resched_lock(&vma->vm_mm->page_table_lock);
}

static inline void smaps_pmd_range(struct vm_area_struct *vma, pud_t *pud,
				unsigned long addr, unsigned long end,
				struct mem_size_stats *mss)
{
	pmd_t *pmd;
	unsigned long next;

	pmd = pmd_offset(pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		if (pmd_none_or_clear_bad(pmd))
			continue;
		smaps_pte_range(vma, pmd, addr, next, mss);
	} while (pmd++, addr = next, addr != end);
}

static inline void smaps_pud_range(struct vm_area_struct *vma, pgd_t *pgd,
				unsigned long addr, unsigned long end,
				struct mem_size_stats *mss)
{
	pud_t *pud;
	unsigned long next;

	pud = pud_offset(pgd, addr);
	do {
		next = pud_addr_end(addr, end);
		if (pud_none_or_clear_bad(pud))
			continue;
		smaps_pmd_range(vma, pud, addr, next, mss);
	} while (pud++, addr = next, addr != end);
}

static inline void smaps_pgd_range(struct vm_area_struct *vma,
				unsigned long addr, unsigned long end,
				struct mem_size_stats *mss)
{
	pgd_t *pgd;
	unsigned long next;

	pgd = pgd_offset(vma->vm_mm, addr);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(pgd))
			continue;
		smaps_pud_range(vma, pgd, addr, next, mss);
	} while (pgd++, addr = next, addr != end);
}

static int show_smap(struct seq_file *m, void *v)
{
	struct vm_area_struct *vma = v;
	struct mm_struct *mm = vma->vm_mm;
	struct mem_size_stats mss;

	memset(&mss, 0, sizeof mss);

	if (mm) {
		spin_lock(&mm->page_table_lock);
		smaps_pgd_range(vma, vma->vm_start, vma->vm_end, &mss);
		spin_unlock(&mm->page_table_lock);
	}

	return show_map_internal(m, v, &mss);
}

static void *m_start(struct seq_file *m, loff_t *pos)
{
	struct task_struct *task = m->private;
	unsigned long last_addr = m->version;
	struct mm_struct *mm;
	struct vm_area_struct *vma, *tail_vma;
	loff_t l = *pos;

	/*
	 * We remember last_addr rather than next_addr to hit with
	 * mmap_cache most of the time. We have zero last_addr at
	 * the beginning and also after lseek. We will have -1 last_addr
	 * after the end of the vmas.
	 */

	if (last_addr == -1UL)
		return NULL;

	mm = get_task_mm(task);
	if (!mm)
		return NULL;

	tail_vma = get_gate_vma(task);
	down_read(&mm->mmap_sem);

	/* Start with last addr hint */
	if (last_addr && (vma = find_vma(mm, last_addr))) {
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

static void m_stop(struct seq_file *m, void *v)
{
	struct task_struct *task = m->private;
	struct vm_area_struct *vma = v;
	if (vma && vma != get_gate_vma(task)) {
		struct mm_struct *mm = vma->vm_mm;
		up_read(&mm->mmap_sem);
		mmput(mm);
	}
}

static void *m_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct task_struct *task = m->private;
	struct vm_area_struct *vma = v;
	struct vm_area_struct *tail_vma = get_gate_vma(task);

	(*pos)++;
	if (vma && (vma != tail_vma) && vma->vm_next)
		return vma->vm_next;
	m_stop(m, v);
	return (vma != tail_vma)? tail_vma: NULL;
}

struct seq_operations proc_pid_maps_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= show_map
};

struct seq_operations proc_pid_smaps_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= show_smap
};

#ifdef CONFIG_NUMA

struct numa_maps {
	unsigned long pages;
	unsigned long anon;
	unsigned long mapped;
	unsigned long mapcount_max;
	unsigned long node[MAX_NUMNODES];
};

/*
 * Calculate numa node maps for a vma
 */
static struct numa_maps *get_numa_maps(const struct vm_area_struct *vma)
{
	struct page *page;
	unsigned long vaddr;
	struct mm_struct *mm = vma->vm_mm;
	int i;
	struct numa_maps *md = kmalloc(sizeof(struct numa_maps), GFP_KERNEL);

	if (!md)
		return NULL;
	md->pages = 0;
	md->anon = 0;
	md->mapped = 0;
	md->mapcount_max = 0;
	for_each_node(i)
		md->node[i] =0;

	spin_lock(&mm->page_table_lock);
 	for (vaddr = vma->vm_start; vaddr < vma->vm_end; vaddr += PAGE_SIZE) {
		page = follow_page(mm, vaddr, 0);
		if (page) {
			int count = page_mapcount(page);

			if (count)
				md->mapped++;
			if (count > md->mapcount_max)
				md->mapcount_max = count;
			md->pages++;
			if (PageAnon(page))
				md->anon++;
			md->node[page_to_nid(page)]++;
		}
	}
	spin_unlock(&mm->page_table_lock);
	return md;
}

static int show_numa_map(struct seq_file *m, void *v)
{
	struct task_struct *task = m->private;
	struct vm_area_struct *vma = v;
	struct mempolicy *pol;
	struct numa_maps *md;
	struct zone **z;
	int n;
	int first;

	if (!vma->vm_mm)
		return 0;

	md = get_numa_maps(vma);
	if (!md)
		return 0;

	seq_printf(m, "%08lx", vma->vm_start);
	pol = get_vma_policy(task, vma, vma->vm_start);
	/* Print policy */
	switch (pol->policy) {
	case MPOL_PREFERRED:
		seq_printf(m, " prefer=%d", pol->v.preferred_node);
		break;
	case MPOL_BIND:
		seq_printf(m, " bind={");
		first = 1;
		for (z = pol->v.zonelist->zones; *z; z++) {

			if (!first)
				seq_putc(m, ',');
			else
				first = 0;
			seq_printf(m, "%d/%s", (*z)->zone_pgdat->node_id,
					(*z)->name);
		}
		seq_putc(m, '}');
		break;
	case MPOL_INTERLEAVE:
		seq_printf(m, " interleave={");
		first = 1;
		for_each_node(n) {
			if (test_bit(n, pol->v.nodes)) {
				if (!first)
					seq_putc(m,',');
				else
					first = 0;
				seq_printf(m, "%d",n);
			}
		}
		seq_putc(m, '}');
		break;
	default:
		seq_printf(m," default");
		break;
	}
	seq_printf(m, " MaxRef=%lu Pages=%lu Mapped=%lu",
			md->mapcount_max, md->pages, md->mapped);
	if (md->anon)
		seq_printf(m," Anon=%lu",md->anon);

	for_each_online_node(n) {
		if (md->node[n])
			seq_printf(m, " N%d=%lu", n, md->node[n]);
	}
	seq_putc(m, '\n');
	kfree(md);
	if (m->count < m->size)  /* vma is copied successfully */
		m->version = (vma != get_gate_vma(task)) ? vma->vm_start : 0;
	return 0;
}

struct seq_operations proc_pid_numa_maps_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= show_numa_map
};
#endif
