/*
 *   linux/mm/fremap.c
 * 
 * Explicit pagetable population and nonlinear (random) mappings support.
 *
 * started by Ingo Molnar, Copyright (C) 2002, 2003
 */

#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/file.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/swapops.h>
#include <linux/rmap.h>
#include <linux/module.h>
#include <linux/syscalls.h>

#include <asm/mmu_context.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

static int zap_pte(struct mm_struct *mm, struct vm_area_struct *vma,
			unsigned long addr, pte_t *ptep)
{
	pte_t pte = *ptep;
	struct page *page = NULL;

	if (pte_present(pte)) {
		flush_cache_page(vma, addr, pte_pfn(pte));
		pte = ptep_clear_flush(vma, addr, ptep);
		page = vm_normal_page(vma, addr, pte);
		if (page) {
			if (pte_dirty(pte))
				set_page_dirty(page);
			page_remove_rmap(page, vma);
			page_cache_release(page);
		}
	} else {
		if (!pte_file(pte))
			free_swap_and_cache(pte_to_swp_entry(pte));
		pte_clear_not_present_full(mm, addr, ptep, 0);
	}
	return !!page;
}

/*
 * Install a file page to a given virtual memory address, release any
 * previously existing mapping.
 */
int install_page(struct mm_struct *mm, struct vm_area_struct *vma,
		unsigned long addr, struct page *page, pgprot_t prot)
{
	struct inode *inode;
	pgoff_t size;
	int err = -ENOMEM;
	pte_t *pte;
	pte_t pte_val;
	spinlock_t *ptl;

	pte = get_locked_pte(mm, addr, &ptl);
	if (!pte)
		goto out;

	/*
	 * This page may have been truncated. Tell the
	 * caller about it.
	 */
	err = -EINVAL;
	inode = vma->vm_file->f_mapping->host;
	size = (i_size_read(inode) + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	if (!page->mapping || page->index >= size)
		goto unlock;
	err = -ENOMEM;
	if (page_mapcount(page) > INT_MAX/2)
		goto unlock;

	if (pte_none(*pte) || !zap_pte(mm, vma, addr, pte))
		inc_mm_counter(mm, file_rss);

	flush_icache_page(vma, page);
	pte_val = mk_pte(page, prot);
	set_pte_at(mm, addr, pte, pte_val);
	page_add_file_rmap(page);
	update_mmu_cache(vma, addr, pte_val);
	lazy_mmu_prot_update(pte_val);
	err = 0;
unlock:
	pte_unmap_unlock(pte, ptl);
out:
	return err;
}
EXPORT_SYMBOL(install_page);

/*
 * Install a file pte to a given virtual memory address, release any
 * previously existing mapping.
 */
int install_file_pte(struct mm_struct *mm, struct vm_area_struct *vma,
		unsigned long addr, unsigned long pgoff, pgprot_t prot)
{
	int err = -ENOMEM;
	pte_t *pte;
	spinlock_t *ptl;

	pte = get_locked_pte(mm, addr, &ptl);
	if (!pte)
		goto out;

	if (!pte_none(*pte) && zap_pte(mm, vma, addr, pte)) {
		update_hiwater_rss(mm);
		dec_mm_counter(mm, file_rss);
	}

	set_pte_at(mm, addr, pte, pgoff_to_pte(pgoff));
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

/***
 * sys_remap_file_pages - remap arbitrary pages of a shared backing store
 *                        file within an existing vma.
 * @start: start of the remapped virtual memory range
 * @size: size of the remapped virtual memory range
 * @prot: new protection bits of the range
 * @pgoff: to be mapped page of the backing store file
 * @flags: 0 or MAP_NONBLOCKED - the later will cause no IO.
 *
 * this syscall works purely via pagetables, so it's the most efficient
 * way to map the same (large) file into a given virtual window. Unlike
 * mmap()/mremap() it does not create any new vmas. The new mappings are
 * also safe across swapout.
 *
 * NOTE: the 'prot' parameter right now is ignored, and the vma's default
 * protection is used. Arbitrary protections might be implemented in the
 * future.
 */
asmlinkage long sys_remap_file_pages(unsigned long start, unsigned long size,
	unsigned long __prot, unsigned long pgoff, unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct address_space *mapping;
	unsigned long end = start + size;
	struct vm_area_struct *vma;
	int err = -EINVAL;
	int has_write_lock = 0;

	if (__prot)
		return err;
	/*
	 * Sanitize the syscall parameters:
	 */
	start = start & PAGE_MASK;
	size = size & PAGE_MASK;

	/* Does the address range wrap, or is the span zero-sized? */
	if (start + size <= start)
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
	 * the single existing vma.  vm_private_data is used as a
	 * swapout cursor in a VM_NONLINEAR vma.
	 */
	if (vma && (vma->vm_flags & VM_SHARED) &&
		(!vma->vm_private_data || (vma->vm_flags & VM_NONLINEAR)) &&
		vma->vm_ops && vma->vm_ops->populate &&
			end > start && start >= vma->vm_start &&
				end <= vma->vm_end) {

		/* Must set VM_NONLINEAR before any pages are populated. */
		if (pgoff != linear_page_index(vma, start) &&
		    !(vma->vm_flags & VM_NONLINEAR)) {
			if (!has_write_lock) {
				up_read(&mm->mmap_sem);
				down_write(&mm->mmap_sem);
				has_write_lock = 1;
				goto retry;
			}
			mapping = vma->vm_file->f_mapping;
			spin_lock(&mapping->i_mmap_lock);
			flush_dcache_mmap_lock(mapping);
			vma->vm_flags |= VM_NONLINEAR;
			vma_prio_tree_remove(vma, &mapping->i_mmap);
			vma_nonlinear_insert(vma, &mapping->i_mmap_nonlinear);
			flush_dcache_mmap_unlock(mapping);
			spin_unlock(&mapping->i_mmap_lock);
		}

		err = vma->vm_ops->populate(vma, start, size,
					    vma->vm_page_prot,
					    pgoff, flags & MAP_NONBLOCK);

		/*
		 * We can't clear VM_NONLINEAR because we'd have to do
		 * it after ->populate completes, and that would prevent
		 * downgrading the lock.  (Locks can't be upgraded).
		 */
	}
	if (likely(!has_write_lock))
		up_read(&mm->mmap_sem);
	else
		up_write(&mm->mmap_sem);

	return err;
}

