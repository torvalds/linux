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
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/hugetlb.h>
#include <linux/writeback.h>
#include <linux/file.h>
#include <linux/syscalls.h>

#include <asm/pgtable.h>
#include <asm/tlbflush.h>

static unsigned long msync_pte_range(struct vm_area_struct *vma, pmd_t *pmd,
				unsigned long addr, unsigned long end)
{
	pte_t *pte;
	spinlock_t *ptl;
	int progress = 0;
	unsigned long ret = 0;

again:
	pte = pte_offset_map_lock(vma->vm_mm, pmd, addr, &ptl);
	do {
		struct page *page;

		if (progress >= 64) {
			progress = 0;
			if (need_resched() || need_lockbreak(ptl))
				break;
		}
		progress++;
		if (!pte_present(*pte))
			continue;
		if (!pte_maybe_dirty(*pte))
			continue;
		page = vm_normal_page(vma, addr, *pte);
		if (!page)
			continue;
		if (ptep_clear_flush_dirty(vma, addr, pte) ||
				page_test_and_clear_dirty(page))
			ret += set_page_dirty(page);
		progress += 3;
	} while (pte++, addr += PAGE_SIZE, addr != end);
	pte_unmap_unlock(pte - 1, ptl);
	cond_resched();
	if (addr != end)
		goto again;
	return ret;
}

static inline unsigned long msync_pmd_range(struct vm_area_struct *vma,
			pud_t *pud, unsigned long addr, unsigned long end)
{
	pmd_t *pmd;
	unsigned long next;
	unsigned long ret = 0;

	pmd = pmd_offset(pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		if (pmd_none_or_clear_bad(pmd))
			continue;
		ret += msync_pte_range(vma, pmd, addr, next);
	} while (pmd++, addr = next, addr != end);
	return ret;
}

static inline unsigned long msync_pud_range(struct vm_area_struct *vma,
			pgd_t *pgd, unsigned long addr, unsigned long end)
{
	pud_t *pud;
	unsigned long next;
	unsigned long ret = 0;

	pud = pud_offset(pgd, addr);
	do {
		next = pud_addr_end(addr, end);
		if (pud_none_or_clear_bad(pud))
			continue;
		ret += msync_pmd_range(vma, pud, addr, next);
	} while (pud++, addr = next, addr != end);
	return ret;
}

static unsigned long msync_page_range(struct vm_area_struct *vma,
				unsigned long addr, unsigned long end)
{
	pgd_t *pgd;
	unsigned long next;
	unsigned long ret = 0;

	/* For hugepages we can't go walking the page table normally,
	 * but that's ok, hugetlbfs is memory based, so we don't need
	 * to do anything more on an msync().
	 */
	if (vma->vm_flags & VM_HUGETLB)
		return 0;

	BUG_ON(addr >= end);
	pgd = pgd_offset(vma->vm_mm, addr);
	flush_cache_range(vma, addr, end);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(pgd))
			continue;
		ret += msync_pud_range(vma, pgd, addr, next);
	} while (pgd++, addr = next, addr != end);
	return ret;
}

/*
 * MS_SYNC syncs the entire file - including mappings.
 *
 * MS_ASYNC does not start I/O (it used to, up to 2.5.67).  Instead, it just
 * marks the relevant pages dirty.  The application may now run fsync() to
 * write out the dirty pages and wait on the writeout and check the result.
 * Or the application may run fadvise(FADV_DONTNEED) against the fd to start
 * async writeout immediately.
 * So by _not_ starting I/O in MS_ASYNC we provide complete flexibility to
 * applications.
 */
static int msync_interval(struct vm_area_struct *vma, unsigned long addr,
			unsigned long end, int flags,
			unsigned long *nr_pages_dirtied)
{
	struct file *file = vma->vm_file;

	if ((flags & MS_INVALIDATE) && (vma->vm_flags & VM_LOCKED))
		return -EBUSY;

	if (file && (vma->vm_flags & VM_SHARED))
		*nr_pages_dirtied = msync_page_range(vma, addr, end);
	return 0;
}

asmlinkage long sys_msync(unsigned long start, size_t len, int flags)
{
	unsigned long end;
	struct vm_area_struct *vma;
	int unmapped_error = 0;
	int error = -EINVAL;
	int done = 0;

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
	down_read(&current->mm->mmap_sem);
	if (flags & MS_SYNC)
		current->flags |= PF_SYNCWRITE;
	vma = find_vma(current->mm, start);
	if (!vma) {
		error = -ENOMEM;
		goto out_unlock;
	}
	do {
		unsigned long nr_pages_dirtied = 0;
		struct file *file;

		/* Here start < vma->vm_end. */
		if (start < vma->vm_start) {
			unmapped_error = -ENOMEM;
			start = vma->vm_start;
		}
		/* Here vma->vm_start <= start < vma->vm_end. */
		if (end <= vma->vm_end) {
			if (start < end) {
				error = msync_interval(vma, start, end, flags,
							&nr_pages_dirtied);
				if (error)
					goto out_unlock;
			}
			error = unmapped_error;
			done = 1;
		} else {
			/* Here vma->vm_start <= start < vma->vm_end < end. */
			error = msync_interval(vma, start, vma->vm_end, flags,
						&nr_pages_dirtied);
			if (error)
				goto out_unlock;
		}
		file = vma->vm_file;
		start = vma->vm_end;
		if ((flags & MS_ASYNC) && file && nr_pages_dirtied) {
			get_file(file);
			up_read(&current->mm->mmap_sem);
			balance_dirty_pages_ratelimited_nr(file->f_mapping,
							nr_pages_dirtied);
			fput(file);
			down_read(&current->mm->mmap_sem);
			vma = find_vma(current->mm, start);
		} else if ((flags & MS_SYNC) && file &&
				(vma->vm_flags & VM_SHARED)) {
			get_file(file);
			up_read(&current->mm->mmap_sem);
			error = do_fsync(file, 0);
			fput(file);
			down_read(&current->mm->mmap_sem);
			if (error)
				goto out_unlock;
			vma = find_vma(current->mm, start);
		} else {
			vma = vma->vm_next;
		}
	} while (vma && !done);
out_unlock:
	current->flags &= ~PF_SYNCWRITE;
	up_read(&current->mm->mmap_sem);
out:
	return error;
}
