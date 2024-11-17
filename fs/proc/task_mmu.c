// SPDX-License-Identifier: GPL-2.0
#include <linux/pagewalk.h>
#include <linux/mm_inline.h>
#include <linux/hugetlb.h>
#include <linux/huge_mm.h>
#include <linux/mount.h>
#include <linux/ksm.h>
#include <linux/seq_file.h>
#include <linux/highmem.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/mempolicy.h>
#include <linux/rmap.h>
#include <linux/swap.h>
#include <linux/sched/mm.h>
#include <linux/swapops.h>
#include <linux/mmu_notifier.h>
#include <linux/page_idle.h>
#include <linux/shmem_fs.h>
#include <linux/uaccess.h>
#include <linux/pkeys.h>
#include <linux/minmax.h>
#include <linux/overflow.h>
#include <linux/buildid.h>

#include <asm/elf.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include "internal.h"

#define SEQ_PUT_DEC(str, val) \
		seq_put_decimal_ull_width(m, str, (val) << (PAGE_SHIFT-10), 8)
void task_mem(struct seq_file *m, struct mm_struct *mm)
{
	unsigned long text, lib, swap, anon, file, shmem;
	unsigned long hiwater_vm, total_vm, hiwater_rss, total_rss;

	anon = get_mm_counter(mm, MM_ANONPAGES);
	file = get_mm_counter(mm, MM_FILEPAGES);
	shmem = get_mm_counter(mm, MM_SHMEMPAGES);

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
	hiwater_rss = total_rss = anon + file + shmem;
	if (hiwater_rss < mm->hiwater_rss)
		hiwater_rss = mm->hiwater_rss;

	/* split executable areas between text and lib */
	text = PAGE_ALIGN(mm->end_code) - (mm->start_code & PAGE_MASK);
	text = min(text, mm->exec_vm << PAGE_SHIFT);
	lib = (mm->exec_vm << PAGE_SHIFT) - text;

	swap = get_mm_counter(mm, MM_SWAPENTS);
	SEQ_PUT_DEC("VmPeak:\t", hiwater_vm);
	SEQ_PUT_DEC(" kB\nVmSize:\t", total_vm);
	SEQ_PUT_DEC(" kB\nVmLck:\t", mm->locked_vm);
	SEQ_PUT_DEC(" kB\nVmPin:\t", atomic64_read(&mm->pinned_vm));
	SEQ_PUT_DEC(" kB\nVmHWM:\t", hiwater_rss);
	SEQ_PUT_DEC(" kB\nVmRSS:\t", total_rss);
	SEQ_PUT_DEC(" kB\nRssAnon:\t", anon);
	SEQ_PUT_DEC(" kB\nRssFile:\t", file);
	SEQ_PUT_DEC(" kB\nRssShmem:\t", shmem);
	SEQ_PUT_DEC(" kB\nVmData:\t", mm->data_vm);
	SEQ_PUT_DEC(" kB\nVmStk:\t", mm->stack_vm);
	seq_put_decimal_ull_width(m,
		    " kB\nVmExe:\t", text >> 10, 8);
	seq_put_decimal_ull_width(m,
		    " kB\nVmLib:\t", lib >> 10, 8);
	seq_put_decimal_ull_width(m,
		    " kB\nVmPTE:\t", mm_pgtables_bytes(mm) >> 10, 8);
	SEQ_PUT_DEC(" kB\nVmSwap:\t", swap);
	seq_puts(m, " kB\n");
	hugetlb_report_usage(m, mm);
}
#undef SEQ_PUT_DEC

unsigned long task_vsize(struct mm_struct *mm)
{
	return PAGE_SIZE * mm->total_vm;
}

unsigned long task_statm(struct mm_struct *mm,
			 unsigned long *shared, unsigned long *text,
			 unsigned long *data, unsigned long *resident)
{
	*shared = get_mm_counter(mm, MM_FILEPAGES) +
			get_mm_counter(mm, MM_SHMEMPAGES);
	*text = (PAGE_ALIGN(mm->end_code) - (mm->start_code & PAGE_MASK))
								>> PAGE_SHIFT;
	*data = mm->data_vm + mm->stack_vm;
	*resident = *shared + get_mm_counter(mm, MM_ANONPAGES);
	return mm->total_vm;
}

#ifdef CONFIG_NUMA
/*
 * Save get_task_policy() for show_numa_map().
 */
static void hold_task_mempolicy(struct proc_maps_private *priv)
{
	struct task_struct *task = priv->task;

	task_lock(task);
	priv->task_mempolicy = get_task_policy(task);
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

static struct vm_area_struct *proc_get_vma(struct proc_maps_private *priv,
						loff_t *ppos)
{
	struct vm_area_struct *vma = vma_next(&priv->iter);

	if (vma) {
		*ppos = vma->vm_start;
	} else {
		*ppos = -2UL;
		vma = get_gate_vma(priv->mm);
	}

	return vma;
}

static void *m_start(struct seq_file *m, loff_t *ppos)
{
	struct proc_maps_private *priv = m->private;
	unsigned long last_addr = *ppos;
	struct mm_struct *mm;

	/* See m_next(). Zero at the start or after lseek. */
	if (last_addr == -1UL)
		return NULL;

	priv->task = get_proc_task(priv->inode);
	if (!priv->task)
		return ERR_PTR(-ESRCH);

	mm = priv->mm;
	if (!mm || !mmget_not_zero(mm)) {
		put_task_struct(priv->task);
		priv->task = NULL;
		return NULL;
	}

	if (mmap_read_lock_killable(mm)) {
		mmput(mm);
		put_task_struct(priv->task);
		priv->task = NULL;
		return ERR_PTR(-EINTR);
	}

	vma_iter_init(&priv->iter, mm, last_addr);
	hold_task_mempolicy(priv);
	if (last_addr == -2UL)
		return get_gate_vma(mm);

	return proc_get_vma(priv, ppos);
}

static void *m_next(struct seq_file *m, void *v, loff_t *ppos)
{
	if (*ppos == -2UL) {
		*ppos = -1UL;
		return NULL;
	}
	return proc_get_vma(m->private, ppos);
}

static void m_stop(struct seq_file *m, void *v)
{
	struct proc_maps_private *priv = m->private;
	struct mm_struct *mm = priv->mm;

	if (!priv->task)
		return;

	release_task_mempolicy(priv);
	mmap_read_unlock(mm);
	mmput(mm);
	put_task_struct(priv->task);
	priv->task = NULL;
}

static int proc_maps_open(struct inode *inode, struct file *file,
			const struct seq_operations *ops, int psize)
{
	struct proc_maps_private *priv = __seq_open_private(file, ops, psize);

	if (!priv)
		return -ENOMEM;

	priv->inode = inode;
	priv->mm = proc_mem_open(inode, PTRACE_MODE_READ);
	if (IS_ERR(priv->mm)) {
		int err = PTR_ERR(priv->mm);

		seq_release_private(inode, file);
		return err;
	}

	return 0;
}

static int proc_map_release(struct inode *inode, struct file *file)
{
	struct seq_file *seq = file->private_data;
	struct proc_maps_private *priv = seq->private;

	if (priv->mm)
		mmdrop(priv->mm);

	return seq_release_private(inode, file);
}

static int do_maps_open(struct inode *inode, struct file *file,
			const struct seq_operations *ops)
{
	return proc_maps_open(inode, file, ops,
				sizeof(struct proc_maps_private));
}

static void get_vma_name(struct vm_area_struct *vma,
			 const struct path **path,
			 const char **name,
			 const char **name_fmt)
{
	struct anon_vma_name *anon_name = vma->vm_mm ? anon_vma_name(vma) : NULL;

	*name = NULL;
	*path = NULL;
	*name_fmt = NULL;

	/*
	 * Print the dentry name for named mappings, and a
	 * special [heap] marker for the heap:
	 */
	if (vma->vm_file) {
		/*
		 * If user named this anon shared memory via
		 * prctl(PR_SET_VMA ..., use the provided name.
		 */
		if (anon_name) {
			*name_fmt = "[anon_shmem:%s]";
			*name = anon_name->name;
		} else {
			*path = file_user_path(vma->vm_file);
		}
		return;
	}

	if (vma->vm_ops && vma->vm_ops->name) {
		*name = vma->vm_ops->name(vma);
		if (*name)
			return;
	}

	*name = arch_vma_name(vma);
	if (*name)
		return;

	if (!vma->vm_mm) {
		*name = "[vdso]";
		return;
	}

	if (vma_is_initial_heap(vma)) {
		*name = "[heap]";
		return;
	}

	if (vma_is_initial_stack(vma)) {
		*name = "[stack]";
		return;
	}

	if (anon_name) {
		*name_fmt = "[anon:%s]";
		*name = anon_name->name;
		return;
	}
}

static void show_vma_header_prefix(struct seq_file *m,
				   unsigned long start, unsigned long end,
				   vm_flags_t flags, unsigned long long pgoff,
				   dev_t dev, unsigned long ino)
{
	seq_setwidth(m, 25 + sizeof(void *) * 6 - 1);
	seq_put_hex_ll(m, NULL, start, 8);
	seq_put_hex_ll(m, "-", end, 8);
	seq_putc(m, ' ');
	seq_putc(m, flags & VM_READ ? 'r' : '-');
	seq_putc(m, flags & VM_WRITE ? 'w' : '-');
	seq_putc(m, flags & VM_EXEC ? 'x' : '-');
	seq_putc(m, flags & VM_MAYSHARE ? 's' : 'p');
	seq_put_hex_ll(m, " ", pgoff, 8);
	seq_put_hex_ll(m, " ", MAJOR(dev), 2);
	seq_put_hex_ll(m, ":", MINOR(dev), 2);
	seq_put_decimal_ull(m, " ", ino);
	seq_putc(m, ' ');
}

static void
show_map_vma(struct seq_file *m, struct vm_area_struct *vma)
{
	const struct path *path;
	const char *name_fmt, *name;
	vm_flags_t flags = vma->vm_flags;
	unsigned long ino = 0;
	unsigned long long pgoff = 0;
	unsigned long start, end;
	dev_t dev = 0;

	if (vma->vm_file) {
		const struct inode *inode = file_user_inode(vma->vm_file);

		dev = inode->i_sb->s_dev;
		ino = inode->i_ino;
		pgoff = ((loff_t)vma->vm_pgoff) << PAGE_SHIFT;
	}

	start = vma->vm_start;
	end = vma->vm_end;
	show_vma_header_prefix(m, start, end, flags, pgoff, dev, ino);

	get_vma_name(vma, &path, &name, &name_fmt);
	if (path) {
		seq_pad(m, ' ');
		seq_path(m, path, "\n");
	} else if (name_fmt) {
		seq_pad(m, ' ');
		seq_printf(m, name_fmt, name);
	} else if (name) {
		seq_pad(m, ' ');
		seq_puts(m, name);
	}
	seq_putc(m, '\n');
}

static int show_map(struct seq_file *m, void *v)
{
	show_map_vma(m, v);
	return 0;
}

static const struct seq_operations proc_pid_maps_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= show_map
};

static int pid_maps_open(struct inode *inode, struct file *file)
{
	return do_maps_open(inode, file, &proc_pid_maps_op);
}

#define PROCMAP_QUERY_VMA_FLAGS (				\
		PROCMAP_QUERY_VMA_READABLE |			\
		PROCMAP_QUERY_VMA_WRITABLE |			\
		PROCMAP_QUERY_VMA_EXECUTABLE |			\
		PROCMAP_QUERY_VMA_SHARED			\
)

#define PROCMAP_QUERY_VALID_FLAGS_MASK (			\
		PROCMAP_QUERY_COVERING_OR_NEXT_VMA |		\
		PROCMAP_QUERY_FILE_BACKED_VMA |			\
		PROCMAP_QUERY_VMA_FLAGS				\
)

static int query_vma_setup(struct mm_struct *mm)
{
	return mmap_read_lock_killable(mm);
}

static void query_vma_teardown(struct mm_struct *mm, struct vm_area_struct *vma)
{
	mmap_read_unlock(mm);
}

static struct vm_area_struct *query_vma_find_by_addr(struct mm_struct *mm, unsigned long addr)
{
	return find_vma(mm, addr);
}

static struct vm_area_struct *query_matching_vma(struct mm_struct *mm,
						 unsigned long addr, u32 flags)
{
	struct vm_area_struct *vma;

next_vma:
	vma = query_vma_find_by_addr(mm, addr);
	if (!vma)
		goto no_vma;

	/* user requested only file-backed VMA, keep iterating */
	if ((flags & PROCMAP_QUERY_FILE_BACKED_VMA) && !vma->vm_file)
		goto skip_vma;

	/* VMA permissions should satisfy query flags */
	if (flags & PROCMAP_QUERY_VMA_FLAGS) {
		u32 perm = 0;

		if (flags & PROCMAP_QUERY_VMA_READABLE)
			perm |= VM_READ;
		if (flags & PROCMAP_QUERY_VMA_WRITABLE)
			perm |= VM_WRITE;
		if (flags & PROCMAP_QUERY_VMA_EXECUTABLE)
			perm |= VM_EXEC;
		if (flags & PROCMAP_QUERY_VMA_SHARED)
			perm |= VM_MAYSHARE;

		if ((vma->vm_flags & perm) != perm)
			goto skip_vma;
	}

	/* found covering VMA or user is OK with the matching next VMA */
	if ((flags & PROCMAP_QUERY_COVERING_OR_NEXT_VMA) || vma->vm_start <= addr)
		return vma;

skip_vma:
	/*
	 * If the user needs closest matching VMA, keep iterating.
	 */
	addr = vma->vm_end;
	if (flags & PROCMAP_QUERY_COVERING_OR_NEXT_VMA)
		goto next_vma;

no_vma:
	return ERR_PTR(-ENOENT);
}

static int do_procmap_query(struct proc_maps_private *priv, void __user *uarg)
{
	struct procmap_query karg;
	struct vm_area_struct *vma;
	struct mm_struct *mm;
	const char *name = NULL;
	char build_id_buf[BUILD_ID_SIZE_MAX], *name_buf = NULL;
	__u64 usize;
	int err;

	if (copy_from_user(&usize, (void __user *)uarg, sizeof(usize)))
		return -EFAULT;
	/* argument struct can never be that large, reject abuse */
	if (usize > PAGE_SIZE)
		return -E2BIG;
	/* argument struct should have at least query_flags and query_addr fields */
	if (usize < offsetofend(struct procmap_query, query_addr))
		return -EINVAL;
	err = copy_struct_from_user(&karg, sizeof(karg), uarg, usize);
	if (err)
		return err;

	/* reject unknown flags */
	if (karg.query_flags & ~PROCMAP_QUERY_VALID_FLAGS_MASK)
		return -EINVAL;
	/* either both buffer address and size are set, or both should be zero */
	if (!!karg.vma_name_size != !!karg.vma_name_addr)
		return -EINVAL;
	if (!!karg.build_id_size != !!karg.build_id_addr)
		return -EINVAL;

	mm = priv->mm;
	if (!mm || !mmget_not_zero(mm))
		return -ESRCH;

	err = query_vma_setup(mm);
	if (err) {
		mmput(mm);
		return err;
	}

	vma = query_matching_vma(mm, karg.query_addr, karg.query_flags);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		vma = NULL;
		goto out;
	}

	karg.vma_start = vma->vm_start;
	karg.vma_end = vma->vm_end;

	karg.vma_flags = 0;
	if (vma->vm_flags & VM_READ)
		karg.vma_flags |= PROCMAP_QUERY_VMA_READABLE;
	if (vma->vm_flags & VM_WRITE)
		karg.vma_flags |= PROCMAP_QUERY_VMA_WRITABLE;
	if (vma->vm_flags & VM_EXEC)
		karg.vma_flags |= PROCMAP_QUERY_VMA_EXECUTABLE;
	if (vma->vm_flags & VM_MAYSHARE)
		karg.vma_flags |= PROCMAP_QUERY_VMA_SHARED;

	karg.vma_page_size = vma_kernel_pagesize(vma);

	if (vma->vm_file) {
		const struct inode *inode = file_user_inode(vma->vm_file);

		karg.vma_offset = ((__u64)vma->vm_pgoff) << PAGE_SHIFT;
		karg.dev_major = MAJOR(inode->i_sb->s_dev);
		karg.dev_minor = MINOR(inode->i_sb->s_dev);
		karg.inode = inode->i_ino;
	} else {
		karg.vma_offset = 0;
		karg.dev_major = 0;
		karg.dev_minor = 0;
		karg.inode = 0;
	}

	if (karg.build_id_size) {
		__u32 build_id_sz;

		err = build_id_parse(vma, build_id_buf, &build_id_sz);
		if (err) {
			karg.build_id_size = 0;
		} else {
			if (karg.build_id_size < build_id_sz) {
				err = -ENAMETOOLONG;
				goto out;
			}
			karg.build_id_size = build_id_sz;
		}
	}

	if (karg.vma_name_size) {
		size_t name_buf_sz = min_t(size_t, PATH_MAX, karg.vma_name_size);
		const struct path *path;
		const char *name_fmt;
		size_t name_sz = 0;

		get_vma_name(vma, &path, &name, &name_fmt);

		if (path || name_fmt || name) {
			name_buf = kmalloc(name_buf_sz, GFP_KERNEL);
			if (!name_buf) {
				err = -ENOMEM;
				goto out;
			}
		}
		if (path) {
			name = d_path(path, name_buf, name_buf_sz);
			if (IS_ERR(name)) {
				err = PTR_ERR(name);
				goto out;
			}
			name_sz = name_buf + name_buf_sz - name;
		} else if (name || name_fmt) {
			name_sz = 1 + snprintf(name_buf, name_buf_sz, name_fmt ?: "%s", name);
			name = name_buf;
		}
		if (name_sz > name_buf_sz) {
			err = -ENAMETOOLONG;
			goto out;
		}
		karg.vma_name_size = name_sz;
	}

	/* unlock vma or mmap_lock, and put mm_struct before copying data to user */
	query_vma_teardown(mm, vma);
	mmput(mm);

	if (karg.vma_name_size && copy_to_user(u64_to_user_ptr(karg.vma_name_addr),
					       name, karg.vma_name_size)) {
		kfree(name_buf);
		return -EFAULT;
	}
	kfree(name_buf);

	if (karg.build_id_size && copy_to_user(u64_to_user_ptr(karg.build_id_addr),
					       build_id_buf, karg.build_id_size))
		return -EFAULT;

	if (copy_to_user(uarg, &karg, min_t(size_t, sizeof(karg), usize)))
		return -EFAULT;

	return 0;

out:
	query_vma_teardown(mm, vma);
	mmput(mm);
	kfree(name_buf);
	return err;
}

static long procfs_procmap_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct seq_file *seq = file->private_data;
	struct proc_maps_private *priv = seq->private;

	switch (cmd) {
	case PROCMAP_QUERY:
		return do_procmap_query(priv, (void __user *)arg);
	default:
		return -ENOIOCTLCMD;
	}
}

const struct file_operations proc_pid_maps_operations = {
	.open		= pid_maps_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= proc_map_release,
	.unlocked_ioctl = procfs_procmap_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
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
	unsigned long resident;
	unsigned long shared_clean;
	unsigned long shared_dirty;
	unsigned long private_clean;
	unsigned long private_dirty;
	unsigned long referenced;
	unsigned long anonymous;
	unsigned long lazyfree;
	unsigned long anonymous_thp;
	unsigned long shmem_thp;
	unsigned long file_thp;
	unsigned long swap;
	unsigned long shared_hugetlb;
	unsigned long private_hugetlb;
	unsigned long ksm;
	u64 pss;
	u64 pss_anon;
	u64 pss_file;
	u64 pss_shmem;
	u64 pss_dirty;
	u64 pss_locked;
	u64 swap_pss;
};

static void smaps_page_accumulate(struct mem_size_stats *mss,
		struct folio *folio, unsigned long size, unsigned long pss,
		bool dirty, bool locked, bool private)
{
	mss->pss += pss;

	if (folio_test_anon(folio))
		mss->pss_anon += pss;
	else if (folio_test_swapbacked(folio))
		mss->pss_shmem += pss;
	else
		mss->pss_file += pss;

	if (locked)
		mss->pss_locked += pss;

	if (dirty || folio_test_dirty(folio)) {
		mss->pss_dirty += pss;
		if (private)
			mss->private_dirty += size;
		else
			mss->shared_dirty += size;
	} else {
		if (private)
			mss->private_clean += size;
		else
			mss->shared_clean += size;
	}
}

static void smaps_account(struct mem_size_stats *mss, struct page *page,
		bool compound, bool young, bool dirty, bool locked,
		bool present)
{
	struct folio *folio = page_folio(page);
	int i, nr = compound ? compound_nr(page) : 1;
	unsigned long size = nr * PAGE_SIZE;

	/*
	 * First accumulate quantities that depend only on |size| and the type
	 * of the compound page.
	 */
	if (folio_test_anon(folio)) {
		mss->anonymous += size;
		if (!folio_test_swapbacked(folio) && !dirty &&
		    !folio_test_dirty(folio))
			mss->lazyfree += size;
	}

	if (folio_test_ksm(folio))
		mss->ksm += size;

	mss->resident += size;
	/* Accumulate the size in pages that have been accessed. */
	if (young || folio_test_young(folio) || folio_test_referenced(folio))
		mss->referenced += size;

	/*
	 * Then accumulate quantities that may depend on sharing, or that may
	 * differ page-by-page.
	 *
	 * refcount == 1 for present entries guarantees that the folio is mapped
	 * exactly once. For large folios this implies that exactly one
	 * PTE/PMD/... maps (a part of) this folio.
	 *
	 * Treat all non-present entries (where relying on the mapcount and
	 * refcount doesn't make sense) as "maybe shared, but not sure how
	 * often". We treat device private entries as being fake-present.
	 *
	 * Note that it would not be safe to read the mapcount especially for
	 * pages referenced by migration entries, even with the PTL held.
	 */
	if (folio_ref_count(folio) == 1 || !present) {
		smaps_page_accumulate(mss, folio, size, size << PSS_SHIFT,
				      dirty, locked, present);
		return;
	}
	/*
	 * We obtain a snapshot of the mapcount. Without holding the folio lock
	 * this snapshot can be slightly wrong as we cannot always read the
	 * mapcount atomically.
	 */
	for (i = 0; i < nr; i++, page++) {
		int mapcount = folio_precise_page_mapcount(folio, page);
		unsigned long pss = PAGE_SIZE << PSS_SHIFT;
		if (mapcount >= 2)
			pss /= mapcount;
		smaps_page_accumulate(mss, folio, PAGE_SIZE, pss,
				dirty, locked, mapcount < 2);
	}
}

#ifdef CONFIG_SHMEM
static int smaps_pte_hole(unsigned long addr, unsigned long end,
			  __always_unused int depth, struct mm_walk *walk)
{
	struct mem_size_stats *mss = walk->private;
	struct vm_area_struct *vma = walk->vma;

	mss->swap += shmem_partial_swap_usage(walk->vma->vm_file->f_mapping,
					      linear_page_index(vma, addr),
					      linear_page_index(vma, end));

	return 0;
}
#else
#define smaps_pte_hole		NULL
#endif /* CONFIG_SHMEM */

static void smaps_pte_hole_lookup(unsigned long addr, struct mm_walk *walk)
{
#ifdef CONFIG_SHMEM
	if (walk->ops->pte_hole) {
		/* depth is not used */
		smaps_pte_hole(addr, addr + PAGE_SIZE, 0, walk);
	}
#endif
}

static void smaps_pte_entry(pte_t *pte, unsigned long addr,
		struct mm_walk *walk)
{
	struct mem_size_stats *mss = walk->private;
	struct vm_area_struct *vma = walk->vma;
	bool locked = !!(vma->vm_flags & VM_LOCKED);
	struct page *page = NULL;
	bool present = false, young = false, dirty = false;
	pte_t ptent = ptep_get(pte);

	if (pte_present(ptent)) {
		page = vm_normal_page(vma, addr, ptent);
		young = pte_young(ptent);
		dirty = pte_dirty(ptent);
		present = true;
	} else if (is_swap_pte(ptent)) {
		swp_entry_t swpent = pte_to_swp_entry(ptent);

		if (!non_swap_entry(swpent)) {
			int mapcount;

			mss->swap += PAGE_SIZE;
			mapcount = swp_swapcount(swpent);
			if (mapcount >= 2) {
				u64 pss_delta = (u64)PAGE_SIZE << PSS_SHIFT;

				do_div(pss_delta, mapcount);
				mss->swap_pss += pss_delta;
			} else {
				mss->swap_pss += (u64)PAGE_SIZE << PSS_SHIFT;
			}
		} else if (is_pfn_swap_entry(swpent)) {
			if (is_device_private_entry(swpent))
				present = true;
			page = pfn_swap_entry_to_page(swpent);
		}
	} else {
		smaps_pte_hole_lookup(addr, walk);
		return;
	}

	if (!page)
		return;

	smaps_account(mss, page, false, young, dirty, locked, present);
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static void smaps_pmd_entry(pmd_t *pmd, unsigned long addr,
		struct mm_walk *walk)
{
	struct mem_size_stats *mss = walk->private;
	struct vm_area_struct *vma = walk->vma;
	bool locked = !!(vma->vm_flags & VM_LOCKED);
	struct page *page = NULL;
	bool present = false;
	struct folio *folio;

	if (pmd_present(*pmd)) {
		page = vm_normal_page_pmd(vma, addr, *pmd);
		present = true;
	} else if (unlikely(thp_migration_supported() && is_swap_pmd(*pmd))) {
		swp_entry_t entry = pmd_to_swp_entry(*pmd);

		if (is_pfn_swap_entry(entry))
			page = pfn_swap_entry_to_page(entry);
	}
	if (IS_ERR_OR_NULL(page))
		return;
	folio = page_folio(page);
	if (folio_test_anon(folio))
		mss->anonymous_thp += HPAGE_PMD_SIZE;
	else if (folio_test_swapbacked(folio))
		mss->shmem_thp += HPAGE_PMD_SIZE;
	else if (folio_is_zone_device(folio))
		/* pass */;
	else
		mss->file_thp += HPAGE_PMD_SIZE;

	smaps_account(mss, page, true, pmd_young(*pmd), pmd_dirty(*pmd),
		      locked, present);
}
#else
static void smaps_pmd_entry(pmd_t *pmd, unsigned long addr,
		struct mm_walk *walk)
{
}
#endif

static int smaps_pte_range(pmd_t *pmd, unsigned long addr, unsigned long end,
			   struct mm_walk *walk)
{
	struct vm_area_struct *vma = walk->vma;
	pte_t *pte;
	spinlock_t *ptl;

	ptl = pmd_trans_huge_lock(pmd, vma);
	if (ptl) {
		smaps_pmd_entry(pmd, addr, walk);
		spin_unlock(ptl);
		goto out;
	}

	pte = pte_offset_map_lock(vma->vm_mm, pmd, addr, &ptl);
	if (!pte) {
		walk->action = ACTION_AGAIN;
		return 0;
	}
	for (; addr != end; pte++, addr += PAGE_SIZE)
		smaps_pte_entry(pte, addr, walk);
	pte_unmap_unlock(pte - 1, ptl);
out:
	cond_resched();
	return 0;
}

static void show_smap_vma_flags(struct seq_file *m, struct vm_area_struct *vma)
{
	/*
	 * Don't forget to update Documentation/ on changes.
	 *
	 * The length of the second argument of mnemonics[]
	 * needs to be 3 instead of previously set 2
	 * (i.e. from [BITS_PER_LONG][2] to [BITS_PER_LONG][3])
	 * to avoid spurious
	 * -Werror=unterminated-string-initialization warning
	 *  with GCC 15
	 */
	static const char mnemonics[BITS_PER_LONG][3] = {
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
		[ilog2(VM_LOCKED)]	= "lo",
		[ilog2(VM_IO)]		= "io",
		[ilog2(VM_SEQ_READ)]	= "sr",
		[ilog2(VM_RAND_READ)]	= "rr",
		[ilog2(VM_DONTCOPY)]	= "dc",
		[ilog2(VM_DONTEXPAND)]	= "de",
		[ilog2(VM_LOCKONFAULT)]	= "lf",
		[ilog2(VM_ACCOUNT)]	= "ac",
		[ilog2(VM_NORESERVE)]	= "nr",
		[ilog2(VM_HUGETLB)]	= "ht",
		[ilog2(VM_SYNC)]	= "sf",
		[ilog2(VM_ARCH_1)]	= "ar",
		[ilog2(VM_WIPEONFORK)]	= "wf",
		[ilog2(VM_DONTDUMP)]	= "dd",
#ifdef CONFIG_ARM64_BTI
		[ilog2(VM_ARM64_BTI)]	= "bt",
#endif
#ifdef CONFIG_MEM_SOFT_DIRTY
		[ilog2(VM_SOFTDIRTY)]	= "sd",
#endif
		[ilog2(VM_MIXEDMAP)]	= "mm",
		[ilog2(VM_HUGEPAGE)]	= "hg",
		[ilog2(VM_NOHUGEPAGE)]	= "nh",
		[ilog2(VM_MERGEABLE)]	= "mg",
		[ilog2(VM_UFFD_MISSING)]= "um",
		[ilog2(VM_UFFD_WP)]	= "uw",
#ifdef CONFIG_ARM64_MTE
		[ilog2(VM_MTE)]		= "mt",
		[ilog2(VM_MTE_ALLOWED)]	= "",
#endif
#ifdef CONFIG_ARCH_HAS_PKEYS
		/* These come out via ProtectionKey: */
		[ilog2(VM_PKEY_BIT0)]	= "",
		[ilog2(VM_PKEY_BIT1)]	= "",
		[ilog2(VM_PKEY_BIT2)]	= "",
#if VM_PKEY_BIT3
		[ilog2(VM_PKEY_BIT3)]	= "",
#endif
#if VM_PKEY_BIT4
		[ilog2(VM_PKEY_BIT4)]	= "",
#endif
#endif /* CONFIG_ARCH_HAS_PKEYS */
#ifdef CONFIG_HAVE_ARCH_USERFAULTFD_MINOR
		[ilog2(VM_UFFD_MINOR)]	= "ui",
#endif /* CONFIG_HAVE_ARCH_USERFAULTFD_MINOR */
#ifdef CONFIG_X86_USER_SHADOW_STACK
		[ilog2(VM_SHADOW_STACK)] = "ss",
#endif
#if defined(CONFIG_64BIT) || defined(CONFIG_PPC32)
		[ilog2(VM_DROPPABLE)] = "dp",
#endif
#ifdef CONFIG_64BIT
		[ilog2(VM_SEALED)] = "sl",
#endif
	};
	size_t i;

	seq_puts(m, "VmFlags: ");
	for (i = 0; i < BITS_PER_LONG; i++) {
		if (!mnemonics[i][0])
			continue;
		if (vma->vm_flags & (1UL << i))
			seq_printf(m, "%s ", mnemonics[i]);
	}
	seq_putc(m, '\n');
}

#ifdef CONFIG_HUGETLB_PAGE
static int smaps_hugetlb_range(pte_t *pte, unsigned long hmask,
				 unsigned long addr, unsigned long end,
				 struct mm_walk *walk)
{
	struct mem_size_stats *mss = walk->private;
	struct vm_area_struct *vma = walk->vma;
	pte_t ptent = huge_ptep_get(walk->mm, addr, pte);
	struct folio *folio = NULL;
	bool present = false;

	if (pte_present(ptent)) {
		folio = page_folio(pte_page(ptent));
		present = true;
	} else if (is_swap_pte(ptent)) {
		swp_entry_t swpent = pte_to_swp_entry(ptent);

		if (is_pfn_swap_entry(swpent))
			folio = pfn_swap_entry_folio(swpent);
	}

	if (folio) {
		/* We treat non-present entries as "maybe shared". */
		if (!present || folio_likely_mapped_shared(folio) ||
		    hugetlb_pmd_shared(pte))
			mss->shared_hugetlb += huge_page_size(hstate_vma(vma));
		else
			mss->private_hugetlb += huge_page_size(hstate_vma(vma));
	}
	return 0;
}
#else
#define smaps_hugetlb_range	NULL
#endif /* HUGETLB_PAGE */

static const struct mm_walk_ops smaps_walk_ops = {
	.pmd_entry		= smaps_pte_range,
	.hugetlb_entry		= smaps_hugetlb_range,
	.walk_lock		= PGWALK_RDLOCK,
};

static const struct mm_walk_ops smaps_shmem_walk_ops = {
	.pmd_entry		= smaps_pte_range,
	.hugetlb_entry		= smaps_hugetlb_range,
	.pte_hole		= smaps_pte_hole,
	.walk_lock		= PGWALK_RDLOCK,
};

/*
 * Gather mem stats from @vma with the indicated beginning
 * address @start, and keep them in @mss.
 *
 * Use vm_start of @vma as the beginning address if @start is 0.
 */
static void smap_gather_stats(struct vm_area_struct *vma,
		struct mem_size_stats *mss, unsigned long start)
{
	const struct mm_walk_ops *ops = &smaps_walk_ops;

	/* Invalid start */
	if (start >= vma->vm_end)
		return;

	if (vma->vm_file && shmem_mapping(vma->vm_file->f_mapping)) {
		/*
		 * For shared or readonly shmem mappings we know that all
		 * swapped out pages belong to the shmem object, and we can
		 * obtain the swap value much more efficiently. For private
		 * writable mappings, we might have COW pages that are
		 * not affected by the parent swapped out pages of the shmem
		 * object, so we have to distinguish them during the page walk.
		 * Unless we know that the shmem object (or the part mapped by
		 * our VMA) has no swapped out pages at all.
		 */
		unsigned long shmem_swapped = shmem_swap_usage(vma);

		if (!start && (!shmem_swapped || (vma->vm_flags & VM_SHARED) ||
					!(vma->vm_flags & VM_WRITE))) {
			mss->swap += shmem_swapped;
		} else {
			ops = &smaps_shmem_walk_ops;
		}
	}

	/* mmap_lock is held in m_start */
	if (!start)
		walk_page_vma(vma, ops, mss);
	else
		walk_page_range(vma->vm_mm, start, vma->vm_end, ops, mss);
}

#define SEQ_PUT_DEC(str, val) \
		seq_put_decimal_ull_width(m, str, (val) >> 10, 8)

/* Show the contents common for smaps and smaps_rollup */
static void __show_smap(struct seq_file *m, const struct mem_size_stats *mss,
	bool rollup_mode)
{
	SEQ_PUT_DEC("Rss:            ", mss->resident);
	SEQ_PUT_DEC(" kB\nPss:            ", mss->pss >> PSS_SHIFT);
	SEQ_PUT_DEC(" kB\nPss_Dirty:      ", mss->pss_dirty >> PSS_SHIFT);
	if (rollup_mode) {
		/*
		 * These are meaningful only for smaps_rollup, otherwise two of
		 * them are zero, and the other one is the same as Pss.
		 */
		SEQ_PUT_DEC(" kB\nPss_Anon:       ",
			mss->pss_anon >> PSS_SHIFT);
		SEQ_PUT_DEC(" kB\nPss_File:       ",
			mss->pss_file >> PSS_SHIFT);
		SEQ_PUT_DEC(" kB\nPss_Shmem:      ",
			mss->pss_shmem >> PSS_SHIFT);
	}
	SEQ_PUT_DEC(" kB\nShared_Clean:   ", mss->shared_clean);
	SEQ_PUT_DEC(" kB\nShared_Dirty:   ", mss->shared_dirty);
	SEQ_PUT_DEC(" kB\nPrivate_Clean:  ", mss->private_clean);
	SEQ_PUT_DEC(" kB\nPrivate_Dirty:  ", mss->private_dirty);
	SEQ_PUT_DEC(" kB\nReferenced:     ", mss->referenced);
	SEQ_PUT_DEC(" kB\nAnonymous:      ", mss->anonymous);
	SEQ_PUT_DEC(" kB\nKSM:            ", mss->ksm);
	SEQ_PUT_DEC(" kB\nLazyFree:       ", mss->lazyfree);
	SEQ_PUT_DEC(" kB\nAnonHugePages:  ", mss->anonymous_thp);
	SEQ_PUT_DEC(" kB\nShmemPmdMapped: ", mss->shmem_thp);
	SEQ_PUT_DEC(" kB\nFilePmdMapped:  ", mss->file_thp);
	SEQ_PUT_DEC(" kB\nShared_Hugetlb: ", mss->shared_hugetlb);
	seq_put_decimal_ull_width(m, " kB\nPrivate_Hugetlb: ",
				  mss->private_hugetlb >> 10, 7);
	SEQ_PUT_DEC(" kB\nSwap:           ", mss->swap);
	SEQ_PUT_DEC(" kB\nSwapPss:        ",
					mss->swap_pss >> PSS_SHIFT);
	SEQ_PUT_DEC(" kB\nLocked:         ",
					mss->pss_locked >> PSS_SHIFT);
	seq_puts(m, " kB\n");
}

static int show_smap(struct seq_file *m, void *v)
{
	struct vm_area_struct *vma = v;
	struct mem_size_stats mss = {};

	smap_gather_stats(vma, &mss, 0);

	show_map_vma(m, vma);

	SEQ_PUT_DEC("Size:           ", vma->vm_end - vma->vm_start);
	SEQ_PUT_DEC(" kB\nKernelPageSize: ", vma_kernel_pagesize(vma));
	SEQ_PUT_DEC(" kB\nMMUPageSize:    ", vma_mmu_pagesize(vma));
	seq_puts(m, " kB\n");

	__show_smap(m, &mss, false);

	seq_printf(m, "THPeligible:    %8u\n",
		   !!thp_vma_allowable_orders(vma, vma->vm_flags,
			   TVA_SMAPS | TVA_ENFORCE_SYSFS, THP_ORDERS_ALL));

	if (arch_pkeys_enabled())
		seq_printf(m, "ProtectionKey:  %8u\n", vma_pkey(vma));
	show_smap_vma_flags(m, vma);

	return 0;
}

static int show_smaps_rollup(struct seq_file *m, void *v)
{
	struct proc_maps_private *priv = m->private;
	struct mem_size_stats mss = {};
	struct mm_struct *mm = priv->mm;
	struct vm_area_struct *vma;
	unsigned long vma_start = 0, last_vma_end = 0;
	int ret = 0;
	VMA_ITERATOR(vmi, mm, 0);

	priv->task = get_proc_task(priv->inode);
	if (!priv->task)
		return -ESRCH;

	if (!mm || !mmget_not_zero(mm)) {
		ret = -ESRCH;
		goto out_put_task;
	}

	ret = mmap_read_lock_killable(mm);
	if (ret)
		goto out_put_mm;

	hold_task_mempolicy(priv);
	vma = vma_next(&vmi);

	if (unlikely(!vma))
		goto empty_set;

	vma_start = vma->vm_start;
	do {
		smap_gather_stats(vma, &mss, 0);
		last_vma_end = vma->vm_end;

		/*
		 * Release mmap_lock temporarily if someone wants to
		 * access it for write request.
		 */
		if (mmap_lock_is_contended(mm)) {
			vma_iter_invalidate(&vmi);
			mmap_read_unlock(mm);
			ret = mmap_read_lock_killable(mm);
			if (ret) {
				release_task_mempolicy(priv);
				goto out_put_mm;
			}

			/*
			 * After dropping the lock, there are four cases to
			 * consider. See the following example for explanation.
			 *
			 *   +------+------+-----------+
			 *   | VMA1 | VMA2 | VMA3      |
			 *   +------+------+-----------+
			 *   |      |      |           |
			 *  4k     8k     16k         400k
			 *
			 * Suppose we drop the lock after reading VMA2 due to
			 * contention, then we get:
			 *
			 *	last_vma_end = 16k
			 *
			 * 1) VMA2 is freed, but VMA3 exists:
			 *
			 *    vma_next(vmi) will return VMA3.
			 *    In this case, just continue from VMA3.
			 *
			 * 2) VMA2 still exists:
			 *
			 *    vma_next(vmi) will return VMA3.
			 *    In this case, just continue from VMA3.
			 *
			 * 3) No more VMAs can be found:
			 *
			 *    vma_next(vmi) will return NULL.
			 *    No more things to do, just break.
			 *
			 * 4) (last_vma_end - 1) is the middle of a vma (VMA'):
			 *
			 *    vma_next(vmi) will return VMA' whose range
			 *    contains last_vma_end.
			 *    Iterate VMA' from last_vma_end.
			 */
			vma = vma_next(&vmi);
			/* Case 3 above */
			if (!vma)
				break;

			/* Case 1 and 2 above */
			if (vma->vm_start >= last_vma_end) {
				smap_gather_stats(vma, &mss, 0);
				last_vma_end = vma->vm_end;
				continue;
			}

			/* Case 4 above */
			if (vma->vm_end > last_vma_end) {
				smap_gather_stats(vma, &mss, last_vma_end);
				last_vma_end = vma->vm_end;
			}
		}
	} for_each_vma(vmi, vma);

empty_set:
	show_vma_header_prefix(m, vma_start, last_vma_end, 0, 0, 0, 0);
	seq_pad(m, ' ');
	seq_puts(m, "[rollup]\n");

	__show_smap(m, &mss, true);

	release_task_mempolicy(priv);
	mmap_read_unlock(mm);

out_put_mm:
	mmput(mm);
out_put_task:
	put_task_struct(priv->task);
	priv->task = NULL;

	return ret;
}
#undef SEQ_PUT_DEC

static const struct seq_operations proc_pid_smaps_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= show_smap
};

static int pid_smaps_open(struct inode *inode, struct file *file)
{
	return do_maps_open(inode, file, &proc_pid_smaps_op);
}

static int smaps_rollup_open(struct inode *inode, struct file *file)
{
	int ret;
	struct proc_maps_private *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL_ACCOUNT);
	if (!priv)
		return -ENOMEM;

	ret = single_open(file, show_smaps_rollup, priv);
	if (ret)
		goto out_free;

	priv->inode = inode;
	priv->mm = proc_mem_open(inode, PTRACE_MODE_READ);
	if (IS_ERR(priv->mm)) {
		ret = PTR_ERR(priv->mm);

		single_release(inode, file);
		goto out_free;
	}

	return 0;

out_free:
	kfree(priv);
	return ret;
}

static int smaps_rollup_release(struct inode *inode, struct file *file)
{
	struct seq_file *seq = file->private_data;
	struct proc_maps_private *priv = seq->private;

	if (priv->mm)
		mmdrop(priv->mm);

	kfree(priv);
	return single_release(inode, file);
}

const struct file_operations proc_pid_smaps_operations = {
	.open		= pid_smaps_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= proc_map_release,
};

const struct file_operations proc_pid_smaps_rollup_operations = {
	.open		= smaps_rollup_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= smaps_rollup_release,
};

enum clear_refs_types {
	CLEAR_REFS_ALL = 1,
	CLEAR_REFS_ANON,
	CLEAR_REFS_MAPPED,
	CLEAR_REFS_SOFT_DIRTY,
	CLEAR_REFS_MM_HIWATER_RSS,
	CLEAR_REFS_LAST,
};

struct clear_refs_private {
	enum clear_refs_types type;
};

#ifdef CONFIG_MEM_SOFT_DIRTY

static inline bool pte_is_pinned(struct vm_area_struct *vma, unsigned long addr, pte_t pte)
{
	struct folio *folio;

	if (!pte_write(pte))
		return false;
	if (!is_cow_mapping(vma->vm_flags))
		return false;
	if (likely(!test_bit(MMF_HAS_PINNED, &vma->vm_mm->flags)))
		return false;
	folio = vm_normal_folio(vma, addr, pte);
	if (!folio)
		return false;
	return folio_maybe_dma_pinned(folio);
}

static inline void clear_soft_dirty(struct vm_area_struct *vma,
		unsigned long addr, pte_t *pte)
{
	/*
	 * The soft-dirty tracker uses #PF-s to catch writes
	 * to pages, so write-protect the pte as well. See the
	 * Documentation/admin-guide/mm/soft-dirty.rst for full description
	 * of how soft-dirty works.
	 */
	pte_t ptent = ptep_get(pte);

	if (pte_present(ptent)) {
		pte_t old_pte;

		if (pte_is_pinned(vma, addr, ptent))
			return;
		old_pte = ptep_modify_prot_start(vma, addr, pte);
		ptent = pte_wrprotect(old_pte);
		ptent = pte_clear_soft_dirty(ptent);
		ptep_modify_prot_commit(vma, addr, pte, old_pte, ptent);
	} else if (is_swap_pte(ptent)) {
		ptent = pte_swp_clear_soft_dirty(ptent);
		set_pte_at(vma->vm_mm, addr, pte, ptent);
	}
}
#else
static inline void clear_soft_dirty(struct vm_area_struct *vma,
		unsigned long addr, pte_t *pte)
{
}
#endif

#if defined(CONFIG_MEM_SOFT_DIRTY) && defined(CONFIG_TRANSPARENT_HUGEPAGE)
static inline void clear_soft_dirty_pmd(struct vm_area_struct *vma,
		unsigned long addr, pmd_t *pmdp)
{
	pmd_t old, pmd = *pmdp;

	if (pmd_present(pmd)) {
		/* See comment in change_huge_pmd() */
		old = pmdp_invalidate(vma, addr, pmdp);
		if (pmd_dirty(old))
			pmd = pmd_mkdirty(pmd);
		if (pmd_young(old))
			pmd = pmd_mkyoung(pmd);

		pmd = pmd_wrprotect(pmd);
		pmd = pmd_clear_soft_dirty(pmd);

		set_pmd_at(vma->vm_mm, addr, pmdp, pmd);
	} else if (is_migration_entry(pmd_to_swp_entry(pmd))) {
		pmd = pmd_swp_clear_soft_dirty(pmd);
		set_pmd_at(vma->vm_mm, addr, pmdp, pmd);
	}
}
#else
static inline void clear_soft_dirty_pmd(struct vm_area_struct *vma,
		unsigned long addr, pmd_t *pmdp)
{
}
#endif

static int clear_refs_pte_range(pmd_t *pmd, unsigned long addr,
				unsigned long end, struct mm_walk *walk)
{
	struct clear_refs_private *cp = walk->private;
	struct vm_area_struct *vma = walk->vma;
	pte_t *pte, ptent;
	spinlock_t *ptl;
	struct folio *folio;

	ptl = pmd_trans_huge_lock(pmd, vma);
	if (ptl) {
		if (cp->type == CLEAR_REFS_SOFT_DIRTY) {
			clear_soft_dirty_pmd(vma, addr, pmd);
			goto out;
		}

		if (!pmd_present(*pmd))
			goto out;

		folio = pmd_folio(*pmd);

		/* Clear accessed and referenced bits. */
		pmdp_test_and_clear_young(vma, addr, pmd);
		folio_test_clear_young(folio);
		folio_clear_referenced(folio);
out:
		spin_unlock(ptl);
		return 0;
	}

	pte = pte_offset_map_lock(vma->vm_mm, pmd, addr, &ptl);
	if (!pte) {
		walk->action = ACTION_AGAIN;
		return 0;
	}
	for (; addr != end; pte++, addr += PAGE_SIZE) {
		ptent = ptep_get(pte);

		if (cp->type == CLEAR_REFS_SOFT_DIRTY) {
			clear_soft_dirty(vma, addr, pte);
			continue;
		}

		if (!pte_present(ptent))
			continue;

		folio = vm_normal_folio(vma, addr, ptent);
		if (!folio)
			continue;

		/* Clear accessed and referenced bits. */
		ptep_test_and_clear_young(vma, addr, pte);
		folio_test_clear_young(folio);
		folio_clear_referenced(folio);
	}
	pte_unmap_unlock(pte - 1, ptl);
	cond_resched();
	return 0;
}

static int clear_refs_test_walk(unsigned long start, unsigned long end,
				struct mm_walk *walk)
{
	struct clear_refs_private *cp = walk->private;
	struct vm_area_struct *vma = walk->vma;

	if (vma->vm_flags & VM_PFNMAP)
		return 1;

	/*
	 * Writing 1 to /proc/pid/clear_refs affects all pages.
	 * Writing 2 to /proc/pid/clear_refs only affects anonymous pages.
	 * Writing 3 to /proc/pid/clear_refs only affects file mapped pages.
	 * Writing 4 to /proc/pid/clear_refs affects all pages.
	 */
	if (cp->type == CLEAR_REFS_ANON && vma->vm_file)
		return 1;
	if (cp->type == CLEAR_REFS_MAPPED && !vma->vm_file)
		return 1;
	return 0;
}

static const struct mm_walk_ops clear_refs_walk_ops = {
	.pmd_entry		= clear_refs_pte_range,
	.test_walk		= clear_refs_test_walk,
	.walk_lock		= PGWALK_WRLOCK,
};

static ssize_t clear_refs_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	struct task_struct *task;
	char buffer[PROC_NUMBUF] = {};
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	enum clear_refs_types type;
	int itype;
	int rv;

	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;
	if (copy_from_user(buffer, buf, count))
		return -EFAULT;
	rv = kstrtoint(strstrip(buffer), 10, &itype);
	if (rv < 0)
		return rv;
	type = (enum clear_refs_types)itype;
	if (type < CLEAR_REFS_ALL || type >= CLEAR_REFS_LAST)
		return -EINVAL;

	task = get_proc_task(file_inode(file));
	if (!task)
		return -ESRCH;
	mm = get_task_mm(task);
	if (mm) {
		VMA_ITERATOR(vmi, mm, 0);
		struct mmu_notifier_range range;
		struct clear_refs_private cp = {
			.type = type,
		};

		if (mmap_write_lock_killable(mm)) {
			count = -EINTR;
			goto out_mm;
		}
		if (type == CLEAR_REFS_MM_HIWATER_RSS) {
			/*
			 * Writing 5 to /proc/pid/clear_refs resets the peak
			 * resident set size to this mm's current rss value.
			 */
			reset_mm_hiwater_rss(mm);
			goto out_unlock;
		}

		if (type == CLEAR_REFS_SOFT_DIRTY) {
			for_each_vma(vmi, vma) {
				if (!(vma->vm_flags & VM_SOFTDIRTY))
					continue;
				vm_flags_clear(vma, VM_SOFTDIRTY);
				vma_set_page_prot(vma);
			}

			inc_tlb_flush_pending(mm);
			mmu_notifier_range_init(&range, MMU_NOTIFY_SOFT_DIRTY,
						0, mm, 0, -1UL);
			mmu_notifier_invalidate_range_start(&range);
		}
		walk_page_range(mm, 0, -1, &clear_refs_walk_ops, &cp);
		if (type == CLEAR_REFS_SOFT_DIRTY) {
			mmu_notifier_invalidate_range_end(&range);
			flush_tlb_mm(mm);
			dec_tlb_flush_pending(mm);
		}
out_unlock:
		mmap_write_unlock(mm);
out_mm:
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
	bool show_pfn;
};

#define PAGEMAP_WALK_SIZE	(PMD_SIZE)
#define PAGEMAP_WALK_MASK	(PMD_MASK)

#define PM_ENTRY_BYTES		sizeof(pagemap_entry_t)
#define PM_PFRAME_BITS		55
#define PM_PFRAME_MASK		GENMASK_ULL(PM_PFRAME_BITS - 1, 0)
#define PM_SOFT_DIRTY		BIT_ULL(55)
#define PM_MMAP_EXCLUSIVE	BIT_ULL(56)
#define PM_UFFD_WP		BIT_ULL(57)
#define PM_FILE			BIT_ULL(61)
#define PM_SWAP			BIT_ULL(62)
#define PM_PRESENT		BIT_ULL(63)

#define PM_END_OF_BUFFER    1

static inline pagemap_entry_t make_pme(u64 frame, u64 flags)
{
	return (pagemap_entry_t) { .pme = (frame & PM_PFRAME_MASK) | flags };
}

static int add_to_pagemap(pagemap_entry_t *pme, struct pagemapread *pm)
{
	pm->buffer[pm->pos++] = *pme;
	if (pm->pos >= pm->len)
		return PM_END_OF_BUFFER;
	return 0;
}

static int pagemap_pte_hole(unsigned long start, unsigned long end,
			    __always_unused int depth, struct mm_walk *walk)
{
	struct pagemapread *pm = walk->private;
	unsigned long addr = start;
	int err = 0;

	while (addr < end) {
		struct vm_area_struct *vma = find_vma(walk->mm, addr);
		pagemap_entry_t pme = make_pme(0, 0);
		/* End of address space hole, which we mark as non-present. */
		unsigned long hole_end;

		if (vma)
			hole_end = min(end, vma->vm_start);
		else
			hole_end = end;

		for (; addr < hole_end; addr += PAGE_SIZE) {
			err = add_to_pagemap(&pme, pm);
			if (err)
				goto out;
		}

		if (!vma)
			break;

		/* Addresses in the VMA. */
		if (vma->vm_flags & VM_SOFTDIRTY)
			pme = make_pme(0, PM_SOFT_DIRTY);
		for (; addr < min(end, vma->vm_end); addr += PAGE_SIZE) {
			err = add_to_pagemap(&pme, pm);
			if (err)
				goto out;
		}
	}
out:
	return err;
}

static pagemap_entry_t pte_to_pagemap_entry(struct pagemapread *pm,
		struct vm_area_struct *vma, unsigned long addr, pte_t pte)
{
	u64 frame = 0, flags = 0;
	struct page *page = NULL;
	struct folio *folio;

	if (pte_present(pte)) {
		if (pm->show_pfn)
			frame = pte_pfn(pte);
		flags |= PM_PRESENT;
		page = vm_normal_page(vma, addr, pte);
		if (pte_soft_dirty(pte))
			flags |= PM_SOFT_DIRTY;
		if (pte_uffd_wp(pte))
			flags |= PM_UFFD_WP;
	} else if (is_swap_pte(pte)) {
		swp_entry_t entry;
		if (pte_swp_soft_dirty(pte))
			flags |= PM_SOFT_DIRTY;
		if (pte_swp_uffd_wp(pte))
			flags |= PM_UFFD_WP;
		entry = pte_to_swp_entry(pte);
		if (pm->show_pfn) {
			pgoff_t offset;
			/*
			 * For PFN swap offsets, keeping the offset field
			 * to be PFN only to be compatible with old smaps.
			 */
			if (is_pfn_swap_entry(entry))
				offset = swp_offset_pfn(entry);
			else
				offset = swp_offset(entry);
			frame = swp_type(entry) |
			    (offset << MAX_SWAPFILES_SHIFT);
		}
		flags |= PM_SWAP;
		if (is_pfn_swap_entry(entry))
			page = pfn_swap_entry_to_page(entry);
		if (pte_marker_entry_uffd_wp(entry))
			flags |= PM_UFFD_WP;
	}

	if (page) {
		folio = page_folio(page);
		if (!folio_test_anon(folio))
			flags |= PM_FILE;
		if ((flags & PM_PRESENT) &&
		    folio_precise_page_mapcount(folio, page) == 1)
			flags |= PM_MMAP_EXCLUSIVE;
	}
	if (vma->vm_flags & VM_SOFTDIRTY)
		flags |= PM_SOFT_DIRTY;

	return make_pme(frame, flags);
}

static int pagemap_pmd_range(pmd_t *pmdp, unsigned long addr, unsigned long end,
			     struct mm_walk *walk)
{
	struct vm_area_struct *vma = walk->vma;
	struct pagemapread *pm = walk->private;
	spinlock_t *ptl;
	pte_t *pte, *orig_pte;
	int err = 0;
#ifdef CONFIG_TRANSPARENT_HUGEPAGE

	ptl = pmd_trans_huge_lock(pmdp, vma);
	if (ptl) {
		unsigned int idx = (addr & ~PMD_MASK) >> PAGE_SHIFT;
		u64 flags = 0, frame = 0;
		pmd_t pmd = *pmdp;
		struct page *page = NULL;
		struct folio *folio = NULL;

		if (vma->vm_flags & VM_SOFTDIRTY)
			flags |= PM_SOFT_DIRTY;

		if (pmd_present(pmd)) {
			page = pmd_page(pmd);

			flags |= PM_PRESENT;
			if (pmd_soft_dirty(pmd))
				flags |= PM_SOFT_DIRTY;
			if (pmd_uffd_wp(pmd))
				flags |= PM_UFFD_WP;
			if (pm->show_pfn)
				frame = pmd_pfn(pmd) + idx;
		}
#ifdef CONFIG_ARCH_ENABLE_THP_MIGRATION
		else if (is_swap_pmd(pmd)) {
			swp_entry_t entry = pmd_to_swp_entry(pmd);
			unsigned long offset;

			if (pm->show_pfn) {
				if (is_pfn_swap_entry(entry))
					offset = swp_offset_pfn(entry) + idx;
				else
					offset = swp_offset(entry) + idx;
				frame = swp_type(entry) |
					(offset << MAX_SWAPFILES_SHIFT);
			}
			flags |= PM_SWAP;
			if (pmd_swp_soft_dirty(pmd))
				flags |= PM_SOFT_DIRTY;
			if (pmd_swp_uffd_wp(pmd))
				flags |= PM_UFFD_WP;
			VM_BUG_ON(!is_pmd_migration_entry(pmd));
			page = pfn_swap_entry_to_page(entry);
		}
#endif

		if (page) {
			folio = page_folio(page);
			if (!folio_test_anon(folio))
				flags |= PM_FILE;
		}

		for (; addr != end; addr += PAGE_SIZE, idx++) {
			unsigned long cur_flags = flags;
			pagemap_entry_t pme;

			if (folio && (flags & PM_PRESENT) &&
			    folio_precise_page_mapcount(folio, page + idx) == 1)
				cur_flags |= PM_MMAP_EXCLUSIVE;

			pme = make_pme(frame, cur_flags);
			err = add_to_pagemap(&pme, pm);
			if (err)
				break;
			if (pm->show_pfn) {
				if (flags & PM_PRESENT)
					frame++;
				else if (flags & PM_SWAP)
					frame += (1 << MAX_SWAPFILES_SHIFT);
			}
		}
		spin_unlock(ptl);
		return err;
	}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

	/*
	 * We can assume that @vma always points to a valid one and @end never
	 * goes beyond vma->vm_end.
	 */
	orig_pte = pte = pte_offset_map_lock(walk->mm, pmdp, addr, &ptl);
	if (!pte) {
		walk->action = ACTION_AGAIN;
		return err;
	}
	for (; addr < end; pte++, addr += PAGE_SIZE) {
		pagemap_entry_t pme;

		pme = pte_to_pagemap_entry(pm, vma, addr, ptep_get(pte));
		err = add_to_pagemap(&pme, pm);
		if (err)
			break;
	}
	pte_unmap_unlock(orig_pte, ptl);

	cond_resched();

	return err;
}

#ifdef CONFIG_HUGETLB_PAGE
/* This function walks within one hugetlb entry in the single call */
static int pagemap_hugetlb_range(pte_t *ptep, unsigned long hmask,
				 unsigned long addr, unsigned long end,
				 struct mm_walk *walk)
{
	struct pagemapread *pm = walk->private;
	struct vm_area_struct *vma = walk->vma;
	u64 flags = 0, frame = 0;
	int err = 0;
	pte_t pte;

	if (vma->vm_flags & VM_SOFTDIRTY)
		flags |= PM_SOFT_DIRTY;

	pte = huge_ptep_get(walk->mm, addr, ptep);
	if (pte_present(pte)) {
		struct folio *folio = page_folio(pte_page(pte));

		if (!folio_test_anon(folio))
			flags |= PM_FILE;

		if (!folio_likely_mapped_shared(folio) &&
		    !hugetlb_pmd_shared(ptep))
			flags |= PM_MMAP_EXCLUSIVE;

		if (huge_pte_uffd_wp(pte))
			flags |= PM_UFFD_WP;

		flags |= PM_PRESENT;
		if (pm->show_pfn)
			frame = pte_pfn(pte) +
				((addr & ~hmask) >> PAGE_SHIFT);
	} else if (pte_swp_uffd_wp_any(pte)) {
		flags |= PM_UFFD_WP;
	}

	for (; addr != end; addr += PAGE_SIZE) {
		pagemap_entry_t pme = make_pme(frame, flags);

		err = add_to_pagemap(&pme, pm);
		if (err)
			return err;
		if (pm->show_pfn && (flags & PM_PRESENT))
			frame++;
	}

	cond_resched();

	return err;
}
#else
#define pagemap_hugetlb_range	NULL
#endif /* HUGETLB_PAGE */

static const struct mm_walk_ops pagemap_ops = {
	.pmd_entry	= pagemap_pmd_range,
	.pte_hole	= pagemap_pte_hole,
	.hugetlb_entry	= pagemap_hugetlb_range,
	.walk_lock	= PGWALK_RDLOCK,
};

/*
 * /proc/pid/pagemap - an array mapping virtual pages to pfns
 *
 * For each page in the address space, this file contains one 64-bit entry
 * consisting of the following:
 *
 * Bits 0-54  page frame number (PFN) if present
 * Bits 0-4   swap type if swapped
 * Bits 5-54  swap offset if swapped
 * Bit  55    pte is soft-dirty (see Documentation/admin-guide/mm/soft-dirty.rst)
 * Bit  56    page exclusively mapped
 * Bit  57    pte is uffd-wp write-protected
 * Bits 58-60 zero
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
	struct mm_struct *mm = file->private_data;
	struct pagemapread pm;
	unsigned long src;
	unsigned long svpfn;
	unsigned long start_vaddr;
	unsigned long end_vaddr;
	int ret = 0, copied = 0;

	if (!mm || !mmget_not_zero(mm))
		goto out;

	ret = -EINVAL;
	/* file position must be aligned */
	if ((*ppos % PM_ENTRY_BYTES) || (count % PM_ENTRY_BYTES))
		goto out_mm;

	ret = 0;
	if (!count)
		goto out_mm;

	/* do not disclose physical addresses: attack vector */
	pm.show_pfn = file_ns_capable(file, &init_user_ns, CAP_SYS_ADMIN);

	pm.len = (PAGEMAP_WALK_SIZE >> PAGE_SHIFT);
	pm.buffer = kmalloc_array(pm.len, PM_ENTRY_BYTES, GFP_KERNEL);
	ret = -ENOMEM;
	if (!pm.buffer)
		goto out_mm;

	src = *ppos;
	svpfn = src / PM_ENTRY_BYTES;
	end_vaddr = mm->task_size;

	/* watch out for wraparound */
	start_vaddr = end_vaddr;
	if (svpfn <= (ULONG_MAX >> PAGE_SHIFT)) {
		unsigned long end;

		ret = mmap_read_lock_killable(mm);
		if (ret)
			goto out_free;
		start_vaddr = untagged_addr_remote(mm, svpfn << PAGE_SHIFT);
		mmap_read_unlock(mm);

		end = start_vaddr + ((count / PM_ENTRY_BYTES) << PAGE_SHIFT);
		if (end >= start_vaddr && end < mm->task_size)
			end_vaddr = end;
	}

	/* Ensure the address is inside the task */
	if (start_vaddr > mm->task_size)
		start_vaddr = end_vaddr;

	ret = 0;
	while (count && (start_vaddr < end_vaddr)) {
		int len;
		unsigned long end;

		pm.pos = 0;
		end = (start_vaddr + PAGEMAP_WALK_SIZE) & PAGEMAP_WALK_MASK;
		/* overflow ? */
		if (end < start_vaddr || end > end_vaddr)
			end = end_vaddr;
		ret = mmap_read_lock_killable(mm);
		if (ret)
			goto out_free;
		ret = walk_page_range(mm, start_vaddr, end, &pagemap_ops, &pm);
		mmap_read_unlock(mm);
		start_vaddr = end;

		len = min(count, PM_ENTRY_BYTES * pm.pos);
		if (copy_to_user(buf, pm.buffer, len)) {
			ret = -EFAULT;
			goto out_free;
		}
		copied += len;
		buf += len;
		count -= len;
	}
	*ppos += copied;
	if (!ret || ret == PM_END_OF_BUFFER)
		ret = copied;

out_free:
	kfree(pm.buffer);
out_mm:
	mmput(mm);
out:
	return ret;
}

static int pagemap_open(struct inode *inode, struct file *file)
{
	struct mm_struct *mm;

	mm = proc_mem_open(inode, PTRACE_MODE_READ);
	if (IS_ERR(mm))
		return PTR_ERR(mm);
	file->private_data = mm;
	return 0;
}

static int pagemap_release(struct inode *inode, struct file *file)
{
	struct mm_struct *mm = file->private_data;

	if (mm)
		mmdrop(mm);
	return 0;
}

#define PM_SCAN_CATEGORIES	(PAGE_IS_WPALLOWED | PAGE_IS_WRITTEN |	\
				 PAGE_IS_FILE |	PAGE_IS_PRESENT |	\
				 PAGE_IS_SWAPPED | PAGE_IS_PFNZERO |	\
				 PAGE_IS_HUGE | PAGE_IS_SOFT_DIRTY)
#define PM_SCAN_FLAGS		(PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC)

struct pagemap_scan_private {
	struct pm_scan_arg arg;
	unsigned long masks_of_interest, cur_vma_category;
	struct page_region *vec_buf;
	unsigned long vec_buf_len, vec_buf_index, found_pages;
	struct page_region __user *vec_out;
};

static unsigned long pagemap_page_category(struct pagemap_scan_private *p,
					   struct vm_area_struct *vma,
					   unsigned long addr, pte_t pte)
{
	unsigned long categories = 0;

	if (pte_present(pte)) {
		struct page *page;

		categories |= PAGE_IS_PRESENT;
		if (!pte_uffd_wp(pte))
			categories |= PAGE_IS_WRITTEN;

		if (p->masks_of_interest & PAGE_IS_FILE) {
			page = vm_normal_page(vma, addr, pte);
			if (page && !PageAnon(page))
				categories |= PAGE_IS_FILE;
		}

		if (is_zero_pfn(pte_pfn(pte)))
			categories |= PAGE_IS_PFNZERO;
		if (pte_soft_dirty(pte))
			categories |= PAGE_IS_SOFT_DIRTY;
	} else if (is_swap_pte(pte)) {
		swp_entry_t swp;

		categories |= PAGE_IS_SWAPPED;
		if (!pte_swp_uffd_wp_any(pte))
			categories |= PAGE_IS_WRITTEN;

		if (p->masks_of_interest & PAGE_IS_FILE) {
			swp = pte_to_swp_entry(pte);
			if (is_pfn_swap_entry(swp) &&
			    !folio_test_anon(pfn_swap_entry_folio(swp)))
				categories |= PAGE_IS_FILE;
		}
		if (pte_swp_soft_dirty(pte))
			categories |= PAGE_IS_SOFT_DIRTY;
	}

	return categories;
}

static void make_uffd_wp_pte(struct vm_area_struct *vma,
			     unsigned long addr, pte_t *pte, pte_t ptent)
{
	if (pte_present(ptent)) {
		pte_t old_pte;

		old_pte = ptep_modify_prot_start(vma, addr, pte);
		ptent = pte_mkuffd_wp(old_pte);
		ptep_modify_prot_commit(vma, addr, pte, old_pte, ptent);
	} else if (is_swap_pte(ptent)) {
		ptent = pte_swp_mkuffd_wp(ptent);
		set_pte_at(vma->vm_mm, addr, pte, ptent);
	} else {
		set_pte_at(vma->vm_mm, addr, pte,
			   make_pte_marker(PTE_MARKER_UFFD_WP));
	}
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static unsigned long pagemap_thp_category(struct pagemap_scan_private *p,
					  struct vm_area_struct *vma,
					  unsigned long addr, pmd_t pmd)
{
	unsigned long categories = PAGE_IS_HUGE;

	if (pmd_present(pmd)) {
		struct page *page;

		categories |= PAGE_IS_PRESENT;
		if (!pmd_uffd_wp(pmd))
			categories |= PAGE_IS_WRITTEN;

		if (p->masks_of_interest & PAGE_IS_FILE) {
			page = vm_normal_page_pmd(vma, addr, pmd);
			if (page && !PageAnon(page))
				categories |= PAGE_IS_FILE;
		}

		if (is_zero_pfn(pmd_pfn(pmd)))
			categories |= PAGE_IS_PFNZERO;
		if (pmd_soft_dirty(pmd))
			categories |= PAGE_IS_SOFT_DIRTY;
	} else if (is_swap_pmd(pmd)) {
		swp_entry_t swp;

		categories |= PAGE_IS_SWAPPED;
		if (!pmd_swp_uffd_wp(pmd))
			categories |= PAGE_IS_WRITTEN;
		if (pmd_swp_soft_dirty(pmd))
			categories |= PAGE_IS_SOFT_DIRTY;

		if (p->masks_of_interest & PAGE_IS_FILE) {
			swp = pmd_to_swp_entry(pmd);
			if (is_pfn_swap_entry(swp) &&
			    !folio_test_anon(pfn_swap_entry_folio(swp)))
				categories |= PAGE_IS_FILE;
		}
	}

	return categories;
}

static void make_uffd_wp_pmd(struct vm_area_struct *vma,
			     unsigned long addr, pmd_t *pmdp)
{
	pmd_t old, pmd = *pmdp;

	if (pmd_present(pmd)) {
		old = pmdp_invalidate_ad(vma, addr, pmdp);
		pmd = pmd_mkuffd_wp(old);
		set_pmd_at(vma->vm_mm, addr, pmdp, pmd);
	} else if (is_migration_entry(pmd_to_swp_entry(pmd))) {
		pmd = pmd_swp_mkuffd_wp(pmd);
		set_pmd_at(vma->vm_mm, addr, pmdp, pmd);
	}
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

#ifdef CONFIG_HUGETLB_PAGE
static unsigned long pagemap_hugetlb_category(pte_t pte)
{
	unsigned long categories = PAGE_IS_HUGE;

	/*
	 * According to pagemap_hugetlb_range(), file-backed HugeTLB
	 * page cannot be swapped. So PAGE_IS_FILE is not checked for
	 * swapped pages.
	 */
	if (pte_present(pte)) {
		categories |= PAGE_IS_PRESENT;
		if (!huge_pte_uffd_wp(pte))
			categories |= PAGE_IS_WRITTEN;
		if (!PageAnon(pte_page(pte)))
			categories |= PAGE_IS_FILE;
		if (is_zero_pfn(pte_pfn(pte)))
			categories |= PAGE_IS_PFNZERO;
		if (pte_soft_dirty(pte))
			categories |= PAGE_IS_SOFT_DIRTY;
	} else if (is_swap_pte(pte)) {
		categories |= PAGE_IS_SWAPPED;
		if (!pte_swp_uffd_wp_any(pte))
			categories |= PAGE_IS_WRITTEN;
		if (pte_swp_soft_dirty(pte))
			categories |= PAGE_IS_SOFT_DIRTY;
	}

	return categories;
}

static void make_uffd_wp_huge_pte(struct vm_area_struct *vma,
				  unsigned long addr, pte_t *ptep,
				  pte_t ptent)
{
	unsigned long psize;

	if (is_hugetlb_entry_hwpoisoned(ptent) || is_pte_marker(ptent))
		return;

	psize = huge_page_size(hstate_vma(vma));

	if (is_hugetlb_entry_migration(ptent))
		set_huge_pte_at(vma->vm_mm, addr, ptep,
				pte_swp_mkuffd_wp(ptent), psize);
	else if (!huge_pte_none(ptent))
		huge_ptep_modify_prot_commit(vma, addr, ptep, ptent,
					     huge_pte_mkuffd_wp(ptent));
	else
		set_huge_pte_at(vma->vm_mm, addr, ptep,
				make_pte_marker(PTE_MARKER_UFFD_WP), psize);
}
#endif /* CONFIG_HUGETLB_PAGE */

#if defined(CONFIG_TRANSPARENT_HUGEPAGE) || defined(CONFIG_HUGETLB_PAGE)
static void pagemap_scan_backout_range(struct pagemap_scan_private *p,
				       unsigned long addr, unsigned long end)
{
	struct page_region *cur_buf = &p->vec_buf[p->vec_buf_index];

	if (cur_buf->start != addr)
		cur_buf->end = addr;
	else
		cur_buf->start = cur_buf->end = 0;

	p->found_pages -= (end - addr) / PAGE_SIZE;
}
#endif

static bool pagemap_scan_is_interesting_page(unsigned long categories,
					     const struct pagemap_scan_private *p)
{
	categories ^= p->arg.category_inverted;
	if ((categories & p->arg.category_mask) != p->arg.category_mask)
		return false;
	if (p->arg.category_anyof_mask && !(categories & p->arg.category_anyof_mask))
		return false;

	return true;
}

static bool pagemap_scan_is_interesting_vma(unsigned long categories,
					    const struct pagemap_scan_private *p)
{
	unsigned long required = p->arg.category_mask & PAGE_IS_WPALLOWED;

	categories ^= p->arg.category_inverted;
	if ((categories & required) != required)
		return false;

	return true;
}

static int pagemap_scan_test_walk(unsigned long start, unsigned long end,
				  struct mm_walk *walk)
{
	struct pagemap_scan_private *p = walk->private;
	struct vm_area_struct *vma = walk->vma;
	unsigned long vma_category = 0;
	bool wp_allowed = userfaultfd_wp_async(vma) &&
	    userfaultfd_wp_use_markers(vma);

	if (!wp_allowed) {
		/* User requested explicit failure over wp-async capability */
		if (p->arg.flags & PM_SCAN_CHECK_WPASYNC)
			return -EPERM;
		/*
		 * User requires wr-protect, and allows silently skipping
		 * unsupported vmas.
		 */
		if (p->arg.flags & PM_SCAN_WP_MATCHING)
			return 1;
		/*
		 * Then the request doesn't involve wr-protects at all,
		 * fall through to the rest checks, and allow vma walk.
		 */
	}

	if (vma->vm_flags & VM_PFNMAP)
		return 1;

	if (wp_allowed)
		vma_category |= PAGE_IS_WPALLOWED;

	if (vma->vm_flags & VM_SOFTDIRTY)
		vma_category |= PAGE_IS_SOFT_DIRTY;

	if (!pagemap_scan_is_interesting_vma(vma_category, p))
		return 1;

	p->cur_vma_category = vma_category;

	return 0;
}

static bool pagemap_scan_push_range(unsigned long categories,
				    struct pagemap_scan_private *p,
				    unsigned long addr, unsigned long end)
{
	struct page_region *cur_buf = &p->vec_buf[p->vec_buf_index];

	/*
	 * When there is no output buffer provided at all, the sentinel values
	 * won't match here. There is no other way for `cur_buf->end` to be
	 * non-zero other than it being non-empty.
	 */
	if (addr == cur_buf->end && categories == cur_buf->categories) {
		cur_buf->end = end;
		return true;
	}

	if (cur_buf->end) {
		if (p->vec_buf_index >= p->vec_buf_len - 1)
			return false;

		cur_buf = &p->vec_buf[++p->vec_buf_index];
	}

	cur_buf->start = addr;
	cur_buf->end = end;
	cur_buf->categories = categories;

	return true;
}

static int pagemap_scan_output(unsigned long categories,
			       struct pagemap_scan_private *p,
			       unsigned long addr, unsigned long *end)
{
	unsigned long n_pages, total_pages;
	int ret = 0;

	if (!p->vec_buf)
		return 0;

	categories &= p->arg.return_mask;

	n_pages = (*end - addr) / PAGE_SIZE;
	if (check_add_overflow(p->found_pages, n_pages, &total_pages) ||
	    total_pages > p->arg.max_pages) {
		size_t n_too_much = total_pages - p->arg.max_pages;
		*end -= n_too_much * PAGE_SIZE;
		n_pages -= n_too_much;
		ret = -ENOSPC;
	}

	if (!pagemap_scan_push_range(categories, p, addr, *end)) {
		*end = addr;
		n_pages = 0;
		ret = -ENOSPC;
	}

	p->found_pages += n_pages;
	if (ret)
		p->arg.walk_end = *end;

	return ret;
}

static int pagemap_scan_thp_entry(pmd_t *pmd, unsigned long start,
				  unsigned long end, struct mm_walk *walk)
{
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	struct pagemap_scan_private *p = walk->private;
	struct vm_area_struct *vma = walk->vma;
	unsigned long categories;
	spinlock_t *ptl;
	int ret = 0;

	ptl = pmd_trans_huge_lock(pmd, vma);
	if (!ptl)
		return -ENOENT;

	categories = p->cur_vma_category |
		     pagemap_thp_category(p, vma, start, *pmd);

	if (!pagemap_scan_is_interesting_page(categories, p))
		goto out_unlock;

	ret = pagemap_scan_output(categories, p, start, &end);
	if (start == end)
		goto out_unlock;

	if (~p->arg.flags & PM_SCAN_WP_MATCHING)
		goto out_unlock;
	if (~categories & PAGE_IS_WRITTEN)
		goto out_unlock;

	/*
	 * Break huge page into small pages if the WP operation
	 * needs to be performed on a portion of the huge page.
	 */
	if (end != start + HPAGE_SIZE) {
		spin_unlock(ptl);
		split_huge_pmd(vma, pmd, start);
		pagemap_scan_backout_range(p, start, end);
		/* Report as if there was no THP */
		return -ENOENT;
	}

	make_uffd_wp_pmd(vma, start, pmd);
	flush_tlb_range(vma, start, end);
out_unlock:
	spin_unlock(ptl);
	return ret;
#else /* !CONFIG_TRANSPARENT_HUGEPAGE */
	return -ENOENT;
#endif
}

static int pagemap_scan_pmd_entry(pmd_t *pmd, unsigned long start,
				  unsigned long end, struct mm_walk *walk)
{
	struct pagemap_scan_private *p = walk->private;
	struct vm_area_struct *vma = walk->vma;
	unsigned long addr, flush_end = 0;
	pte_t *pte, *start_pte;
	spinlock_t *ptl;
	int ret;

	arch_enter_lazy_mmu_mode();

	ret = pagemap_scan_thp_entry(pmd, start, end, walk);
	if (ret != -ENOENT) {
		arch_leave_lazy_mmu_mode();
		return ret;
	}

	ret = 0;
	start_pte = pte = pte_offset_map_lock(vma->vm_mm, pmd, start, &ptl);
	if (!pte) {
		arch_leave_lazy_mmu_mode();
		walk->action = ACTION_AGAIN;
		return 0;
	}

	if ((p->arg.flags & PM_SCAN_WP_MATCHING) && !p->vec_out) {
		/* Fast path for performing exclusive WP */
		for (addr = start; addr != end; pte++, addr += PAGE_SIZE) {
			pte_t ptent = ptep_get(pte);

			if ((pte_present(ptent) && pte_uffd_wp(ptent)) ||
			    pte_swp_uffd_wp_any(ptent))
				continue;
			make_uffd_wp_pte(vma, addr, pte, ptent);
			if (!flush_end)
				start = addr;
			flush_end = addr + PAGE_SIZE;
		}
		goto flush_and_return;
	}

	if (!p->arg.category_anyof_mask && !p->arg.category_inverted &&
	    p->arg.category_mask == PAGE_IS_WRITTEN &&
	    p->arg.return_mask == PAGE_IS_WRITTEN) {
		for (addr = start; addr < end; pte++, addr += PAGE_SIZE) {
			unsigned long next = addr + PAGE_SIZE;
			pte_t ptent = ptep_get(pte);

			if ((pte_present(ptent) && pte_uffd_wp(ptent)) ||
			    pte_swp_uffd_wp_any(ptent))
				continue;
			ret = pagemap_scan_output(p->cur_vma_category | PAGE_IS_WRITTEN,
						  p, addr, &next);
			if (next == addr)
				break;
			if (~p->arg.flags & PM_SCAN_WP_MATCHING)
				continue;
			make_uffd_wp_pte(vma, addr, pte, ptent);
			if (!flush_end)
				start = addr;
			flush_end = next;
		}
		goto flush_and_return;
	}

	for (addr = start; addr != end; pte++, addr += PAGE_SIZE) {
		pte_t ptent = ptep_get(pte);
		unsigned long categories = p->cur_vma_category |
					   pagemap_page_category(p, vma, addr, ptent);
		unsigned long next = addr + PAGE_SIZE;

		if (!pagemap_scan_is_interesting_page(categories, p))
			continue;

		ret = pagemap_scan_output(categories, p, addr, &next);
		if (next == addr)
			break;

		if (~p->arg.flags & PM_SCAN_WP_MATCHING)
			continue;
		if (~categories & PAGE_IS_WRITTEN)
			continue;

		make_uffd_wp_pte(vma, addr, pte, ptent);
		if (!flush_end)
			start = addr;
		flush_end = next;
	}

flush_and_return:
	if (flush_end)
		flush_tlb_range(vma, start, addr);

	pte_unmap_unlock(start_pte, ptl);
	arch_leave_lazy_mmu_mode();

	cond_resched();
	return ret;
}

#ifdef CONFIG_HUGETLB_PAGE
static int pagemap_scan_hugetlb_entry(pte_t *ptep, unsigned long hmask,
				      unsigned long start, unsigned long end,
				      struct mm_walk *walk)
{
	struct pagemap_scan_private *p = walk->private;
	struct vm_area_struct *vma = walk->vma;
	unsigned long categories;
	spinlock_t *ptl;
	int ret = 0;
	pte_t pte;

	if (~p->arg.flags & PM_SCAN_WP_MATCHING) {
		/* Go the short route when not write-protecting pages. */

		pte = huge_ptep_get(walk->mm, start, ptep);
		categories = p->cur_vma_category | pagemap_hugetlb_category(pte);

		if (!pagemap_scan_is_interesting_page(categories, p))
			return 0;

		return pagemap_scan_output(categories, p, start, &end);
	}

	i_mmap_lock_write(vma->vm_file->f_mapping);
	ptl = huge_pte_lock(hstate_vma(vma), vma->vm_mm, ptep);

	pte = huge_ptep_get(walk->mm, start, ptep);
	categories = p->cur_vma_category | pagemap_hugetlb_category(pte);

	if (!pagemap_scan_is_interesting_page(categories, p))
		goto out_unlock;

	ret = pagemap_scan_output(categories, p, start, &end);
	if (start == end)
		goto out_unlock;

	if (~categories & PAGE_IS_WRITTEN)
		goto out_unlock;

	if (end != start + HPAGE_SIZE) {
		/* Partial HugeTLB page WP isn't possible. */
		pagemap_scan_backout_range(p, start, end);
		p->arg.walk_end = start;
		ret = 0;
		goto out_unlock;
	}

	make_uffd_wp_huge_pte(vma, start, ptep, pte);
	flush_hugetlb_tlb_range(vma, start, end);

out_unlock:
	spin_unlock(ptl);
	i_mmap_unlock_write(vma->vm_file->f_mapping);

	return ret;
}
#else
#define pagemap_scan_hugetlb_entry NULL
#endif

static int pagemap_scan_pte_hole(unsigned long addr, unsigned long end,
				 int depth, struct mm_walk *walk)
{
	struct pagemap_scan_private *p = walk->private;
	struct vm_area_struct *vma = walk->vma;
	int ret, err;

	if (!vma || !pagemap_scan_is_interesting_page(p->cur_vma_category, p))
		return 0;

	ret = pagemap_scan_output(p->cur_vma_category, p, addr, &end);
	if (addr == end)
		return ret;

	if (~p->arg.flags & PM_SCAN_WP_MATCHING)
		return ret;

	err = uffd_wp_range(vma, addr, end - addr, true);
	if (err < 0)
		ret = err;

	return ret;
}

static const struct mm_walk_ops pagemap_scan_ops = {
	.test_walk = pagemap_scan_test_walk,
	.pmd_entry = pagemap_scan_pmd_entry,
	.pte_hole = pagemap_scan_pte_hole,
	.hugetlb_entry = pagemap_scan_hugetlb_entry,
};

static int pagemap_scan_get_args(struct pm_scan_arg *arg,
				 unsigned long uarg)
{
	if (copy_from_user(arg, (void __user *)uarg, sizeof(*arg)))
		return -EFAULT;

	if (arg->size != sizeof(struct pm_scan_arg))
		return -EINVAL;

	/* Validate requested features */
	if (arg->flags & ~PM_SCAN_FLAGS)
		return -EINVAL;
	if ((arg->category_inverted | arg->category_mask |
	     arg->category_anyof_mask | arg->return_mask) & ~PM_SCAN_CATEGORIES)
		return -EINVAL;

	arg->start = untagged_addr((unsigned long)arg->start);
	arg->end = untagged_addr((unsigned long)arg->end);
	arg->vec = untagged_addr((unsigned long)arg->vec);

	/* Validate memory pointers */
	if (!IS_ALIGNED(arg->start, PAGE_SIZE))
		return -EINVAL;
	if (!access_ok((void __user *)(long)arg->start, arg->end - arg->start))
		return -EFAULT;
	if (!arg->vec && arg->vec_len)
		return -EINVAL;
	if (UINT_MAX == SIZE_MAX && arg->vec_len > SIZE_MAX)
		return -EINVAL;
	if (arg->vec && !access_ok((void __user *)(long)arg->vec,
				   size_mul(arg->vec_len, sizeof(struct page_region))))
		return -EFAULT;

	/* Fixup default values */
	arg->end = ALIGN(arg->end, PAGE_SIZE);
	arg->walk_end = 0;
	if (!arg->max_pages)
		arg->max_pages = ULONG_MAX;

	return 0;
}

static int pagemap_scan_writeback_args(struct pm_scan_arg *arg,
				       unsigned long uargl)
{
	struct pm_scan_arg __user *uarg	= (void __user *)uargl;

	if (copy_to_user(&uarg->walk_end, &arg->walk_end, sizeof(arg->walk_end)))
		return -EFAULT;

	return 0;
}

static int pagemap_scan_init_bounce_buffer(struct pagemap_scan_private *p)
{
	if (!p->arg.vec_len)
		return 0;

	p->vec_buf_len = min_t(size_t, PAGEMAP_WALK_SIZE >> PAGE_SHIFT,
			       p->arg.vec_len);
	p->vec_buf = kmalloc_array(p->vec_buf_len, sizeof(*p->vec_buf),
				   GFP_KERNEL);
	if (!p->vec_buf)
		return -ENOMEM;

	p->vec_buf->start = p->vec_buf->end = 0;
	p->vec_out = (struct page_region __user *)(long)p->arg.vec;

	return 0;
}

static long pagemap_scan_flush_buffer(struct pagemap_scan_private *p)
{
	const struct page_region *buf = p->vec_buf;
	long n = p->vec_buf_index;

	if (!p->vec_buf)
		return 0;

	if (buf[n].end != buf[n].start)
		n++;

	if (!n)
		return 0;

	if (copy_to_user(p->vec_out, buf, n * sizeof(*buf)))
		return -EFAULT;

	p->arg.vec_len -= n;
	p->vec_out += n;

	p->vec_buf_index = 0;
	p->vec_buf_len = min_t(size_t, p->vec_buf_len, p->arg.vec_len);
	p->vec_buf->start = p->vec_buf->end = 0;

	return n;
}

static long do_pagemap_scan(struct mm_struct *mm, unsigned long uarg)
{
	struct pagemap_scan_private p = {0};
	unsigned long walk_start;
	size_t n_ranges_out = 0;
	int ret;

	ret = pagemap_scan_get_args(&p.arg, uarg);
	if (ret)
		return ret;

	p.masks_of_interest = p.arg.category_mask | p.arg.category_anyof_mask |
			      p.arg.return_mask;
	ret = pagemap_scan_init_bounce_buffer(&p);
	if (ret)
		return ret;

	for (walk_start = p.arg.start; walk_start < p.arg.end;
			walk_start = p.arg.walk_end) {
		struct mmu_notifier_range range;
		long n_out;

		if (fatal_signal_pending(current)) {
			ret = -EINTR;
			break;
		}

		ret = mmap_read_lock_killable(mm);
		if (ret)
			break;

		/* Protection change for the range is going to happen. */
		if (p.arg.flags & PM_SCAN_WP_MATCHING) {
			mmu_notifier_range_init(&range, MMU_NOTIFY_PROTECTION_VMA, 0,
						mm, walk_start, p.arg.end);
			mmu_notifier_invalidate_range_start(&range);
		}

		ret = walk_page_range(mm, walk_start, p.arg.end,
				      &pagemap_scan_ops, &p);

		if (p.arg.flags & PM_SCAN_WP_MATCHING)
			mmu_notifier_invalidate_range_end(&range);

		mmap_read_unlock(mm);

		n_out = pagemap_scan_flush_buffer(&p);
		if (n_out < 0)
			ret = n_out;
		else
			n_ranges_out += n_out;

		if (ret != -ENOSPC)
			break;

		if (p.arg.vec_len == 0 || p.found_pages == p.arg.max_pages)
			break;
	}

	/* ENOSPC signifies early stop (buffer full) from the walk. */
	if (!ret || ret == -ENOSPC)
		ret = n_ranges_out;

	/* The walk_end isn't set when ret is zero */
	if (!p.arg.walk_end)
		p.arg.walk_end = p.arg.end;
	if (pagemap_scan_writeback_args(&p.arg, uarg))
		ret = -EFAULT;

	kfree(p.vec_buf);
	return ret;
}

static long do_pagemap_cmd(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	struct mm_struct *mm = file->private_data;

	switch (cmd) {
	case PAGEMAP_SCAN:
		return do_pagemap_scan(mm, arg);

	default:
		return -EINVAL;
	}
}

const struct file_operations proc_pagemap_operations = {
	.llseek		= mem_lseek, /* borrow this */
	.read		= pagemap_read,
	.open		= pagemap_open,
	.release	= pagemap_release,
	.unlocked_ioctl = do_pagemap_cmd,
	.compat_ioctl	= do_pagemap_cmd,
};
#endif /* CONFIG_PROC_PAGE_MONITOR */

#ifdef CONFIG_NUMA

struct numa_maps {
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
	struct folio *folio = page_folio(page);
	int count = folio_precise_page_mapcount(folio, page);

	md->pages += nr_pages;
	if (pte_dirty || folio_test_dirty(folio))
		md->dirty += nr_pages;

	if (folio_test_swapcache(folio))
		md->swapcache += nr_pages;

	if (folio_test_active(folio) || folio_test_unevictable(folio))
		md->active += nr_pages;

	if (folio_test_writeback(folio))
		md->writeback += nr_pages;

	if (folio_test_anon(folio))
		md->anon += nr_pages;

	if (count > md->mapcount_max)
		md->mapcount_max = count;

	md->node[folio_nid(folio)] += nr_pages;
}

static struct page *can_gather_numa_stats(pte_t pte, struct vm_area_struct *vma,
		unsigned long addr)
{
	struct page *page;
	int nid;

	if (!pte_present(pte))
		return NULL;

	page = vm_normal_page(vma, addr, pte);
	if (!page || is_zone_device_page(page))
		return NULL;

	if (PageReserved(page))
		return NULL;

	nid = page_to_nid(page);
	if (!node_isset(nid, node_states[N_MEMORY]))
		return NULL;

	return page;
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static struct page *can_gather_numa_stats_pmd(pmd_t pmd,
					      struct vm_area_struct *vma,
					      unsigned long addr)
{
	struct page *page;
	int nid;

	if (!pmd_present(pmd))
		return NULL;

	page = vm_normal_page_pmd(vma, addr, pmd);
	if (!page)
		return NULL;

	if (PageReserved(page))
		return NULL;

	nid = page_to_nid(page);
	if (!node_isset(nid, node_states[N_MEMORY]))
		return NULL;

	return page;
}
#endif

static int gather_pte_stats(pmd_t *pmd, unsigned long addr,
		unsigned long end, struct mm_walk *walk)
{
	struct numa_maps *md = walk->private;
	struct vm_area_struct *vma = walk->vma;
	spinlock_t *ptl;
	pte_t *orig_pte;
	pte_t *pte;

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	ptl = pmd_trans_huge_lock(pmd, vma);
	if (ptl) {
		struct page *page;

		page = can_gather_numa_stats_pmd(*pmd, vma, addr);
		if (page)
			gather_stats(page, md, pmd_dirty(*pmd),
				     HPAGE_PMD_SIZE/PAGE_SIZE);
		spin_unlock(ptl);
		return 0;
	}
#endif
	orig_pte = pte = pte_offset_map_lock(walk->mm, pmd, addr, &ptl);
	if (!pte) {
		walk->action = ACTION_AGAIN;
		return 0;
	}
	do {
		pte_t ptent = ptep_get(pte);
		struct page *page = can_gather_numa_stats(ptent, vma, addr);
		if (!page)
			continue;
		gather_stats(page, md, pte_dirty(ptent), 1);

	} while (pte++, addr += PAGE_SIZE, addr != end);
	pte_unmap_unlock(orig_pte, ptl);
	cond_resched();
	return 0;
}
#ifdef CONFIG_HUGETLB_PAGE
static int gather_hugetlb_stats(pte_t *pte, unsigned long hmask,
		unsigned long addr, unsigned long end, struct mm_walk *walk)
{
	pte_t huge_pte = huge_ptep_get(walk->mm, addr, pte);
	struct numa_maps *md;
	struct page *page;

	if (!pte_present(huge_pte))
		return 0;

	page = pte_page(huge_pte);

	md = walk->private;
	gather_stats(page, md, pte_dirty(huge_pte), 1);
	return 0;
}

#else
static int gather_hugetlb_stats(pte_t *pte, unsigned long hmask,
		unsigned long addr, unsigned long end, struct mm_walk *walk)
{
	return 0;
}
#endif

static const struct mm_walk_ops show_numa_ops = {
	.hugetlb_entry = gather_hugetlb_stats,
	.pmd_entry = gather_pte_stats,
	.walk_lock = PGWALK_RDLOCK,
};

/*
 * Display pages allocated per node and memory policy via /proc.
 */
static int show_numa_map(struct seq_file *m, void *v)
{
	struct numa_maps_private *numa_priv = m->private;
	struct proc_maps_private *proc_priv = &numa_priv->proc_maps;
	struct vm_area_struct *vma = v;
	struct numa_maps *md = &numa_priv->md;
	struct file *file = vma->vm_file;
	struct mm_struct *mm = vma->vm_mm;
	char buffer[64];
	struct mempolicy *pol;
	pgoff_t ilx;
	int nid;

	if (!mm)
		return 0;

	/* Ensure we start with an empty set of numa_maps statistics. */
	memset(md, 0, sizeof(*md));

	pol = __get_vma_policy(vma, vma->vm_start, &ilx);
	if (pol) {
		mpol_to_str(buffer, sizeof(buffer), pol);
		mpol_cond_put(pol);
	} else {
		mpol_to_str(buffer, sizeof(buffer), proc_priv->task_mempolicy);
	}

	seq_printf(m, "%08lx %s", vma->vm_start, buffer);

	if (file) {
		seq_puts(m, " file=");
		seq_path(m, file_user_path(file), "\n\t= ");
	} else if (vma_is_initial_heap(vma)) {
		seq_puts(m, " heap");
	} else if (vma_is_initial_stack(vma)) {
		seq_puts(m, " stack");
	}

	if (is_vm_hugetlb_page(vma))
		seq_puts(m, " huge");

	/* mmap_lock is held by m_start */
	walk_page_vma(vma, &show_numa_ops, md);

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

	for_each_node_state(nid, N_MEMORY)
		if (md->node[nid])
			seq_printf(m, " N%d=%lu", nid, md->node[nid]);

	seq_printf(m, " kernelpagesize_kB=%lu", vma_kernel_pagesize(vma) >> 10);
out:
	seq_putc(m, '\n');
	return 0;
}

static const struct seq_operations proc_pid_numa_maps_op = {
	.start  = m_start,
	.next   = m_next,
	.stop   = m_stop,
	.show   = show_numa_map,
};

static int pid_numa_maps_open(struct inode *inode, struct file *file)
{
	return proc_maps_open(inode, file, &proc_pid_numa_maps_op,
				sizeof(struct numa_maps_private));
}

const struct file_operations proc_pid_numa_maps_operations = {
	.open		= pid_numa_maps_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= proc_map_release,
};

#endif /* CONFIG_NUMA */
