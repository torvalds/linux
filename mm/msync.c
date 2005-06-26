/*
 *	linux/mm/msync.c
 *
 * Copyright (C) 1994-1999  Linus Torvalds
 */

/*
 * The msync() system call.
 */
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/hugetlb.h>
#include <linux/syscalls.h>

#include <asm/pgtable.h>
#include <asm/tlbflush.h>

/*
 * Called with mm->page_table_lock held to protect against other
 * threads/the swapper from ripping pte's out from under us.
 */

static void sync_pte_range(struct vm_area_struct *vma, pmd_t *pmd,
				unsigned long addr, unsigned long end)
{
	pte_t *pte;

	pte = pte_offset_map(pmd, addr);
	do {
		unsigned long pfn;
		struct page *page;

		if (!pte_present(*pte))
			continue;
		if (!pte_maybe_dirty(*pte))
			continue;
		pfn = pte_pfn(*pte);
		if (!pfn_valid(pfn))
			continue;
		page = pfn_to_page(pfn);
		if (PageReserved(page))
			continue;

		if (ptep_clear_flush_dirty(vma, addr, pte) ||
		    page_test_and_clear_dirty(page))
			set_page_dirty(page);
	} while (pte++, addr += PAGE_SIZE, addr != end);
	pte_unmap(pte - 1);
}

static inline void sync_pmd_range(struct vm_area_struct *vma, pud_t *pud,
				unsigned long addr, unsigned long end)
{
	pmd_t *pmd;
	unsigned long next;

	pmd = pmd_offset(pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		if (pmd_none_or_clear_bad(pmd))
			continue;
		sync_pte_range(vma, pmd, addr, next);
	} while (pmd++, addr = next, addr != end);
}

static inline void sync_pud_range(struct vm_area_struct *vma, pgd_t *pgd,
				unsigned long addr, unsigned long end)
{
	pud_t *pud;
	unsigned long next;

	pud = pud_offset(pgd, addr);
	do {
		next = pud_addr_end(addr, end);
		if (pud_none_or_clear_bad(pud))
			continue;
		sync_pmd_range(vma, pud, addr, next);
	} while (pud++, addr = next, addr != end);
}

static void sync_page_range(struct vm_area_struct *vma,
				unsigned long addr, unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;
	pgd_t *pgd;
	unsigned long next;

	/* For hugepages we can't go walking the page table normally,
	 * but that's ok, hugetlbfs is memory based, so we don't need
	 * to do anything more on an msync() */
	if (is_vm_hugetlb_page(vma))
		return;

	BUG_ON(addr >= end);
	pgd = pgd_offset(mm, addr);
	flush_cache_range(vma, addr, end);
	spin_lock(&mm->page_table_lock);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(pgd))
			continue;
		sync_pud_range(vma, pgd, addr, next);
	} while (pgd++, addr = next, addr != end);
	spin_unlock(&mm->page_table_lock);
}

#ifdef CONFIG_PREEMPT
static inline void filemap_sync(struct vm_area_struct *vma,
				unsigned long addr, unsigned long end)
{
	const size_t chunk = 64 * 1024;	/* bytes */
	unsigned long next;

	do {
		next = addr + chunk;
		if (next > end || next < addr)
			next = end;
		sync_page_range(vma, addr, next);
		cond_resched();
	} while (addr = next, addr != end);
}
#else
static inline void filemap_sync(struct vm_area_struct *vma,
				unsigned long addr, unsigned long end)
{
	sync_page_range(vma, addr, end);
}
#endif

/*
 * MS_SYNC syncs the entire file - including mappings.
 *
 * MS_ASYNC does not start I/O (it used to, up to 2.5.67).  Instead, it just
 * marks the relevant pages dirty.  The application may now run fsync() to
 * write out the dirty pages and wait on the writeout and check the result.
 * Or the application may run fadvise(FADV_DONTNEED) against the fd to start
 * async writeout immediately.
 * So my _not_ starting I/O in MS_ASYNC we provide complete flexibility to
 * applications.
 */
static int msync_interval(struct vm_area_struct *vma,
			unsigned long addr, unsigned long end, int flags)
{
	int ret = 0;
	struct file *file = vma->vm_file;

	if ((flags & MS_INVALIDATE) && (vma->vm_flags & VM_LOCKED))
		return -EBUSY;

	if (file && (vma->vm_flags & VM_SHARED)) {
		filemap_sync(vma, addr, end);

		if (flags & MS_SYNC) {
			struct address_space *mapping = file->f_mapping;
			int err;

			ret = filemap_fdatawrite(mapping);
			if (file->f_op && file->f_op->fsync) {
				/*
				 * We don't take i_sem here because mmap_sem
				 * is already held.
				 */
				err = file->f_op->fsync(file,file->f_dentry,1);
				if (err && !ret)
					ret = err;
			}
			err = filemap_fdatawait(mapping);
			if (!ret)
				ret = err;
		}
	}
	return ret;
}

asmlinkage long sys_msync(unsigned long start, size_t len, int flags)
{
	unsigned long end;
	struct vm_area_struct *vma;
	int unmapped_error, error = -EINVAL;

	if (flags & MS_SYNC)
		current->flags |= PF_SYNCWRITE;

	down_read(&current->mm->mmap_sem);
	if (flags & ~(MS_ASYNC | MS_INVALIDATE | MS_SYNC))
		goto out;
	if (start & ~PAGE_MASK)
		goto out;
	if ((flags & MS_ASYNC) && (flags & MS_SYNC))
		goto out;
	error = -ENOMEM;
	len = (len + ~PAGE_MASK) & PAGE_MASK;
	end = start + len;
	if (end < start)
		goto out;
	error = 0;
	if (end == start)
		goto out;
	/*
	 * If the interval [start,end) covers some unmapped address ranges,
	 * just ignore them, but return -ENOMEM at the end.
	 */
	vma = find_vma(current->mm, start);
	unmapped_error = 0;
	for (;;) {
		/* Still start < end. */
		error = -ENOMEM;
		if (!vma)
			goto out;
		/* Here start < vma->vm_end. */
		if (start < vma->vm_start) {
			unmapped_error = -ENOMEM;
			start = vma->vm_start;
		}
		/* Here vma->vm_start <= start < vma->vm_end. */
		if (end <= vma->vm_end) {
			if (start < end) {
				error = msync_interval(vma, start, end, flags);
				if (error)
					goto out;
			}
			error = unmapped_error;
			goto out;
		}
		/* Here vma->vm_start <= start < vma->vm_end < end. */
		error = msync_interval(vma, start, vma->vm_end, flags);
		if (error)
			goto out;
		start = vma->vm_end;
		vma = vma->vm_next;
	}
out:
	up_read(&current->mm->mmap_sem);
	current->flags &= ~PF_SYNCWRITE;
	return error;
}
