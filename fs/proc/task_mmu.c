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
	buffer += sprintf(buffer,
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
	return buffer;
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

int proc_exe_link(struct inode *inode, struct dentry **dentry, struct vfsmount **mnt)
{
	struct vm_area_struct * vma;
	int result = -ENOENT;
	struct task_struct *task = get_proc_task(inode);
	struct mm_struct * mm = NULL;

	if (task) {
		mm = get_task_mm(task);
		put_task_struct(task);
	}
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
		*mnt = mntget(vma->vm_file->f_path.mnt);
		*dentry = dget(vma->vm_file->f_path.dentry);
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
	struct proc_maps_private *priv = m->private;
	struct task_struct *task = priv->task;
	struct vm_area_struct *vma = v;
	struct mm_struct *mm = vma->vm_mm;
	struct file *file = vma->vm_file;
	int flags = vma->vm_flags;
	unsigned long ino = 0;
	dev_t dev = 0;
	int len;

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
		seq_path(m, file->f_path.mnt, file->f_path.dentry, "\n");
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
	return show_map_internal(m, v, NULL);
}

static void smaps_pte_range(struct vm_area_struct *vma, pmd_t *pmd,
				unsigned long addr, unsigned long end,
				struct mem_size_stats *mss)
{
	pte_t *pte, ptent;
	spinlock_t *ptl;
	struct page *page;

	pte = pte_offset_map_lock(vma->vm_mm, pmd, addr, &ptl);
	do {
		ptent = *pte;
		if (!pte_present(ptent))
			continue;

		mss->resident += PAGE_SIZE;

		page = vm_normal_page(vma, addr, ptent);
		if (!page)
			continue;

		if (page_mapcount(page) >= 2) {
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
	pte_unmap_unlock(pte - 1, ptl);
	cond_resched();
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
	struct mem_size_stats mss;

	memset(&mss, 0, sizeof mss);
	if (vma->vm_mm && !is_vm_hugetlb_page(vma))
		smaps_pgd_range(vma, vma->vm_start, vma->vm_end, &mss);
	return show_map_internal(m, v, &mss);
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

	mm = get_task_mm(priv->task);
	if (!mm)
		return NULL;

	priv->tail_vma = tail_vma = get_gate_vma(priv->task);
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

static void vma_stop(struct proc_maps_private *priv, struct vm_area_struct *vma)
{
	if (vma && vma != priv->tail_vma) {
		struct mm_struct *mm = vma->vm_mm;
		up_read(&mm->mmap_sem);
		mmput(mm);
	}
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

static struct seq_operations proc_pid_maps_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= show_map
};

static struct seq_operations proc_pid_smaps_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= show_smap
};

static int do_maps_open(struct inode *inode, struct file *file,
			struct seq_operations *ops)
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

#ifdef CONFIG_NUMA
extern int show_numa_map(struct seq_file *m, void *v);

static struct seq_operations proc_pid_numa_maps_op = {
        .start  = m_start,
        .next   = m_next,
        .stop   = m_stop,
        .show   = show_numa_map
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
