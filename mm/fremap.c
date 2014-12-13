/*
 *   linux/mm/fremap.c
 * 
 * Explicit pagetable population and nonlinear (random) mappings support.
 *
 * started by Ingo Molnar, Copyright (C) 2002, 2003
 */
#include <linux/export.h>
#include <linux/backing-dev.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/file.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/swapops.h>
#include <linux/rmap.h>
#include <linux/syscalls.h>
#include <linux/mmu_notifier.h>

#include <asm/mmu_context.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

#include "internal.h"

static int mm_counter(struct page *page)
{
	return PageAnon(page) ? MM_ANONPAGES : MM_FILEPAGES;
}

static void zap_pte(struct mm_struct *mm, struct vm_area_struct *vma,
			unsigned long addr, pte_t *ptep)
{
	pte_t pte = *ptep;
	struct page *page;
	swp_entry_t entry;

	if (pte_present(pte)) {
		flush_cache_page(vma, addr, pte_pfn(pte));
		pte = ptep_clear_flush(vma, addr, ptep);
		page = vm_normal_page(vma, addr, pte);
		if (page) {
			if (pte_dirty(pte))
				set_page_dirty(page);
			update_hiwater_rss(mm);
			dec_mm_counter(mm, mm_counter(page));
			page_remove_rmap(page);
			page_cache_release(page);
		}
	} else {	/* zap_pte() is not called when pte_none() */
		if (!pte_file(pte)) {
			update_hiwater_rss(mm);
			entry = pte_to_swp_entry(pte);
			if (non_swap_entry(entry)) {
				if (is_migration_entry(entry)) {
					page = migration_entry_to_page(entry);
					dec_mm_counter(mm, mm_counter(page));
				}
			} else {
				free_swap_and_cache(entry);
				dec_mm_counter(mm, MM_SWAPENTS);
			}
		}
		pte_clear_not_present_full(mm, addr, ptep, 0);
	}
}

/*
 * Install a file pte to a given virtual memory address, release any
 * previously existing mapping.
 */
static int install_file_pte(struct mm_struct *mm, struct vm_area_struct *vma,
		unsigned long addr, unsigned long pgoff, pgprot_t prot)
{
	int err = -ENOMEM;
	pte_t *pte, ptfile;
	spinlock_t *ptl;

	pte = get_locked_pte(mm, addr, &ptl);
	if (!pte)
		goto out;

	ptfile = pgoff_to_pte(pgoff);

	if (!pte_none(*pte))
		zap_pte(mm, vma, addr, pte);

	set_pte_at(mm, addr, pte, pte_file_mksoft_dirty(ptfile));
	/*
	 * We don't need to run update_mmu_cache() here because the "file pte"
	 * being installed by install_file_pte() is not a real pte - it's a
	 * non-present entry (like a swap entry), noting what file offset should
	 * be mapped there when there's a fault (in a non-linear vma where
	 * that's not obvious).
	 */
	pte_unmap_unlock(pte, ptl);
	err = 0;
out:
	return err;
}

int generic_file_remap_pages(struct vm_area_struct *vma, unsigned long addr,
			     unsigned long size, pgoff_t pgoff)
{
	struct mm_struct *mm = vma->vm_mm;
	int err;

	do {
		err = install_file_pte(mm, vma, addr, pgoff, vma->vm_page_prot);
		if (err)
			return err;

		size -= PAGE_SIZE;
		addr += PAGE_SIZE;
		pgoff++;
	} while (size);

	return 0;
}
EXPORT_SYMBOL(generic_file_remap_pages);

/**
 * sys_remap_file_pages - remap arbitrary pages of an existing VM_SHARED vma
 * @start: start of the remapped virtual memory range
 * @size: size of the remapped virtual memory range
 * @prot: new protection bits of the range (see NOTE)
 * @pgoff: to-be-mapped page of the backing store file
 * @flags: 0 or MAP_NONBLOCKED - the later will cause no IO.
 *
 * sys_remap_file_pages remaps arbitrary pages of an existing VM_SHARED vma
 * (shared backing store file).
 *
 * This syscall works purely via pagetables, so it's the most efficient
 * way to map the same (large) file into a given virtual window. Unlike
 * mmap()/mremap() it does not create any new vmas. The new mappings are
 * also safe across swapout.
 *
 * NOTE: the @prot parameter right now is ignored (but must be zero),
 * and the vma's default protection is used. Arbitrary protections
 * might be implemented in the future.
 */
SYSCALL_DEFINE5(remap_file_pages, unsigned long, start, unsigned long, size,
		unsigned long, prot, unsigned long, pgoff, unsigned long, flags)
{
	struct mm_struct *mm = current->mm;
	struct address_space *mapping;
	struct vm_area_struct *vma;
	int err = -EINVAL;
	int has_write_lock = 0;
	vm_flags_t vm_flags = 0;

	pr_warn_once("%s (%d) uses deprecated remap_file_pages() syscall. "
			"See Documentation/vm/remap_file_pages.txt.\n",
			current->comm, current->pid);

	if (prot)
		return err;
	/*
	 * Sanitize the syscall parameters:
	 */
	start = start & PAGE_MASK;
	size = size & PAGE_MASK;

	/* Does the address range wrap, or is the span zero-sized? */
	if (start + size <= start)
		return err;

	/* Does pgoff wrap? */
	if (pgoff + (size >> PAGE_SHIFT) < pgoff)
		return err;

	/* Can we represent this offset inside this architecture's pte's? */
#if PTE_FILE_MAX_BITS < BITS_PER_LONG
	if (pgoff + (size >> PAGE_SHIFT) >= (1UL << PTE_FILE_MAX_BITS))
		return err;
#endif

	/* We need down_write() to change vma->vm_flags. */
	down_read(&mm->mmap_sem);
 retry:
	vma = find_vma(mm, start);

	/*
	 * Make sure the vma is shared, that it supports prefaulting,
	 * and that the remapped range is valid and fully within
	 * the single existing vma.
	 */
	if (!vma || !(vma->vm_flags & VM_SHARED))
		goto out;

	if (!vma->vm_ops || !vma->vm_ops->remap_pages)
		goto out;

	if (start < vma->vm_start || start + size > vma->vm_end)
		goto out;

	/* Must set VM_NONLINEAR before any pages are populated. */
	if (!(vma->vm_flags & VM_NONLINEAR)) {
		/*
		 * vm_private_data is used as a swapout cursor
		 * in a VM_NONLINEAR vma.
		 */
		if (vma->vm_private_data)
			goto out;

		/* Don't need a nonlinear mapping, exit success */
		if (pgoff == linear_page_index(vma, start)) {
			err = 0;
			goto out;
		}

		if (!has_write_lock) {
get_write_lock:
			up_read(&mm->mmap_sem);
			down_write(&mm->mmap_sem);
			has_write_lock = 1;
			goto retry;
		}
		mapping = vma->vm_file->f_mapping;
		/*
		 * page_mkclean doesn't work on nonlinear vmas, so if
		 * dirty pages need to be accounted, emulate with linear
		 * vmas.
		 */
		if (mapping_cap_account_dirty(mapping)) {
			unsigned long addr;
			struct file *file = get_file(vma->vm_file);
			/* mmap_region may free vma; grab the info now */
			vm_flags = vma->vm_flags;

			addr = mmap_region(file, start, size, vm_flags, pgoff);
			fput(file);
			if (IS_ERR_VALUE(addr)) {
				err = addr;
			} else {
				BUG_ON(addr != start);
				err = 0;
			}
			goto out_freed;
		}
		i_mmap_lock_write(mapping);
		flush_dcache_mmap_lock(mapping);
		vma->vm_flags |= VM_NONLINEAR;
		vma_interval_tree_remove(vma, &mapping->i_mmap);
		vma_nonlinear_insert(vma, &mapping->i_mmap_nonlinear);
		flush_dcache_mmap_unlock(mapping);
		i_mmap_unlock_write(mapping);
	}

	if (vma->vm_flags & VM_LOCKED) {
		/*
		 * drop PG_Mlocked flag for over-mapped range
		 */
		if (!has_write_lock)
			goto get_write_lock;
		vm_flags = vma->vm_flags;
		munlock_vma_pages_range(vma, start, start + size);
		vma->vm_flags = vm_flags;
	}

	mmu_notifier_invalidate_range_start(mm, start, start + size);
	err = vma->vm_ops->remap_pages(vma, start, size, pgoff);
	mmu_notifier_invalidate_range_end(mm, start, start + size);

	/*
	 * We can't clear VM_NONLINEAR because we'd have to do
	 * it after ->populate completes, and that would prevent
	 * downgrading the lock.  (Locks can't be upgraded).
	 */

out:
	if (vma)
		vm_flags = vma->vm_flags;
out_freed:
	if (likely(!has_write_lock))
		up_read(&mm->mmap_sem);
	else
		up_write(&mm->mmap_sem);
	if (!err && ((vm_flags & VM_LOCKED) || !(flags & MAP_NONBLOCK)))
		mm_populate(start, size);

	return err;
}
