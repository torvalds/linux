/*
 *	mm/mremap.c
 *
 *	(C) Copyright 1996 Linus Torvalds
 *
 *	Address space accounting code	<alan@redhat.com>
 *	(C) Copyright 2002 Red Hat Inc, All Rights Reserved
 */

#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/slab.h>
#include <linux/shm.h>
#include <linux/mman.h>
#include <linux/swap.h>
#include <linux/fs.h>
#include <linux/highmem.h>
#include <linux/security.h>
#include <linux/syscalls.h>

#include <asm/uaccess.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

static pte_t *get_one_pte_map_nested(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte = NULL;

	pgd = pgd_offset(mm, addr);
	if (pgd_none_or_clear_bad(pgd))
		goto end;

	pud = pud_offset(pgd, addr);
	if (pud_none_or_clear_bad(pud))
		goto end;

	pmd = pmd_offset(pud, addr);
	if (pmd_none_or_clear_bad(pmd))
		goto end;

	pte = pte_offset_map_nested(pmd, addr);
	if (pte_none(*pte)) {
		pte_unmap_nested(pte);
		pte = NULL;
	}
end:
	return pte;
}

static pte_t *get_one_pte_map(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;

	pgd = pgd_offset(mm, addr);
	if (pgd_none_or_clear_bad(pgd))
		return NULL;

	pud = pud_offset(pgd, addr);
	if (pud_none_or_clear_bad(pud))
		return NULL;

	pmd = pmd_offset(pud, addr);
	if (pmd_none_or_clear_bad(pmd))
		return NULL;

	return pte_offset_map(pmd, addr);
}

static inline pte_t *alloc_one_pte_map(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte = NULL;

	pgd = pgd_offset(mm, addr);

	pud = pud_alloc(mm, pgd, addr);
	if (!pud)
		return NULL;
	pmd = pmd_alloc(mm, pud, addr);
	if (pmd)
		pte = pte_alloc_map(mm, pmd, addr);
	return pte;
}

static int
move_one_page(struct vm_area_struct *vma, unsigned long old_addr,
		struct vm_area_struct *new_vma, unsigned long new_addr)
{
	struct address_space *mapping = NULL;
	struct mm_struct *mm = vma->vm_mm;
	int error = 0;
	pte_t *src, *dst;

	if (vma->vm_file) {
		/*
		 * Subtle point from Rajesh Venkatasubramanian: before
		 * moving file-based ptes, we must lock vmtruncate out,
		 * since it might clean the dst vma before the src vma,
		 * and we propagate stale pages into the dst afterward.
		 */
		mapping = vma->vm_file->f_mapping;
		spin_lock(&mapping->i_mmap_lock);
		if (new_vma->vm_truncate_count &&
		    new_vma->vm_truncate_count != vma->vm_truncate_count)
			new_vma->vm_truncate_count = 0;
	}
	spin_lock(&mm->page_table_lock);

	src = get_one_pte_map_nested(mm, old_addr);
	if (src) {
		/*
		 * Look to see whether alloc_one_pte_map needs to perform a
		 * memory allocation.  If it does then we need to drop the
		 * atomic kmap
		 */
		dst = get_one_pte_map(mm, new_addr);
		if (unlikely(!dst)) {
			pte_unmap_nested(src);
			if (mapping)
				spin_unlock(&mapping->i_mmap_lock);
			dst = alloc_one_pte_map(mm, new_addr);
			if (mapping && !spin_trylock(&mapping->i_mmap_lock)) {
				spin_unlock(&mm->page_table_lock);
				spin_lock(&mapping->i_mmap_lock);
				spin_lock(&mm->page_table_lock);
			}
			src = get_one_pte_map_nested(mm, old_addr);
		}
		/*
		 * Since alloc_one_pte_map can drop and re-acquire
		 * page_table_lock, we should re-check the src entry...
		 */
		if (src) {
			if (dst) {
				pte_t pte;
				pte = ptep_clear_flush(vma, old_addr, src);

				/* ZERO_PAGE can be dependant on virtual addr */
				pte = move_pte(pte, new_vma->vm_page_prot,
							old_addr, new_addr);
				set_pte_at(mm, new_addr, dst, pte);
			} else
				error = -ENOMEM;
			pte_unmap_nested(src);
		}
		if (dst)
			pte_unmap(dst);
	}
	spin_unlock(&mm->page_table_lock);
	if (mapping)
		spin_unlock(&mapping->i_mmap_lock);
	return error;
}

static unsigned long move_page_tables(struct vm_area_struct *vma,
		unsigned long old_addr, struct vm_area_struct *new_vma,
		unsigned long new_addr, unsigned long len)
{
	unsigned long offset;

	flush_cache_range(vma, old_addr, old_addr + len);

	/*
	 * This is not the clever way to do this, but we're taking the
	 * easy way out on the assumption that most remappings will be
	 * only a few pages.. This also makes error recovery easier.
	 */
	for (offset = 0; offset < len; offset += PAGE_SIZE) {
		if (move_one_page(vma, old_addr + offset,
				new_vma, new_addr + offset) < 0)
			break;
		cond_resched();
	}
	return offset;
}

static unsigned long move_vma(struct vm_area_struct *vma,
		unsigned long old_addr, unsigned long old_len,
		unsigned long new_len, unsigned long new_addr)
{
	struct mm_struct *mm = vma->vm_mm;
	struct vm_area_struct *new_vma;
	unsigned long vm_flags = vma->vm_flags;
	unsigned long new_pgoff;
	unsigned long moved_len;
	unsigned long excess = 0;
	int split = 0;

	/*
	 * We'd prefer to avoid failure later on in do_munmap:
	 * which may split one vma into three before unmapping.
	 */
	if (mm->map_count >= sysctl_max_map_count - 3)
		return -ENOMEM;

	new_pgoff = vma->vm_pgoff + ((old_addr - vma->vm_start) >> PAGE_SHIFT);
	new_vma = copy_vma(&vma, new_addr, new_len, new_pgoff);
	if (!new_vma)
		return -ENOMEM;

	moved_len = move_page_tables(vma, old_addr, new_vma, new_addr, old_len);
	if (moved_len < old_len) {
		/*
		 * On error, move entries back from new area to old,
		 * which will succeed since page tables still there,
		 * and then proceed to unmap new area instead of old.
		 */
		move_page_tables(new_vma, new_addr, vma, old_addr, moved_len);
		vma = new_vma;
		old_len = new_len;
		old_addr = new_addr;
		new_addr = -ENOMEM;
	}

	/* Conceal VM_ACCOUNT so old reservation is not undone */
	if (vm_flags & VM_ACCOUNT) {
		vma->vm_flags &= ~VM_ACCOUNT;
		excess = vma->vm_end - vma->vm_start - old_len;
		if (old_addr > vma->vm_start &&
		    old_addr + old_len < vma->vm_end)
			split = 1;
	}

	/*
	 * if we failed to move page tables we still do total_vm increment
	 * since do_munmap() will decrement it by old_len == new_len
	 */
	mm->total_vm += new_len >> PAGE_SHIFT;
	__vm_stat_account(mm, vma->vm_flags, vma->vm_file, new_len>>PAGE_SHIFT);

	if (do_munmap(mm, old_addr, old_len) < 0) {
		/* OOM: unable to split vma, just get accounts right */
		vm_unacct_memory(excess >> PAGE_SHIFT);
		excess = 0;
	}

	/* Restore VM_ACCOUNT if one or two pieces of vma left */
	if (excess) {
		vma->vm_flags |= VM_ACCOUNT;
		if (split)
			vma->vm_next->vm_flags |= VM_ACCOUNT;
	}

	if (vm_flags & VM_LOCKED) {
		mm->locked_vm += new_len >> PAGE_SHIFT;
		if (new_len > old_len)
			make_pages_present(new_addr + old_len,
					   new_addr + new_len);
	}

	return new_addr;
}

/*
 * Expand (or shrink) an existing mapping, potentially moving it at the
 * same time (controlled by the MREMAP_MAYMOVE flag and available VM space)
 *
 * MREMAP_FIXED option added 5-Dec-1999 by Benjamin LaHaise
 * This option implies MREMAP_MAYMOVE.
 */
unsigned long do_mremap(unsigned long addr,
	unsigned long old_len, unsigned long new_len,
	unsigned long flags, unsigned long new_addr)
{
	struct vm_area_struct *vma;
	unsigned long ret = -EINVAL;
	unsigned long charged = 0;

	if (flags & ~(MREMAP_FIXED | MREMAP_MAYMOVE))
		goto out;

	if (addr & ~PAGE_MASK)
		goto out;

	old_len = PAGE_ALIGN(old_len);
	new_len = PAGE_ALIGN(new_len);

	/*
	 * We allow a zero old-len as a special case
	 * for DOS-emu "duplicate shm area" thing. But
	 * a zero new-len is nonsensical.
	 */
	if (!new_len)
		goto out;

	/* new_addr is only valid if MREMAP_FIXED is specified */
	if (flags & MREMAP_FIXED) {
		if (new_addr & ~PAGE_MASK)
			goto out;
		if (!(flags & MREMAP_MAYMOVE))
			goto out;

		if (new_len > TASK_SIZE || new_addr > TASK_SIZE - new_len)
			goto out;

		/* Check if the location we're moving into overlaps the
		 * old location at all, and fail if it does.
		 */
		if ((new_addr <= addr) && (new_addr+new_len) > addr)
			goto out;

		if ((addr <= new_addr) && (addr+old_len) > new_addr)
			goto out;

		ret = do_munmap(current->mm, new_addr, new_len);
		if (ret)
			goto out;
	}

	/*
	 * Always allow a shrinking remap: that just unmaps
	 * the unnecessary pages..
	 * do_munmap does all the needed commit accounting
	 */
	if (old_len >= new_len) {
		ret = do_munmap(current->mm, addr+new_len, old_len - new_len);
		if (ret && old_len != new_len)
			goto out;
		ret = addr;
		if (!(flags & MREMAP_FIXED) || (new_addr == addr))
			goto out;
		old_len = new_len;
	}

	/*
	 * Ok, we need to grow..  or relocate.
	 */
	ret = -EFAULT;
	vma = find_vma(current->mm, addr);
	if (!vma || vma->vm_start > addr)
		goto out;
	if (is_vm_hugetlb_page(vma)) {
		ret = -EINVAL;
		goto out;
	}
	/* We can't remap across vm area boundaries */
	if (old_len > vma->vm_end - addr)
		goto out;
	if (vma->vm_flags & VM_DONTEXPAND) {
		if (new_len > old_len)
			goto out;
	}
	if (vma->vm_flags & VM_LOCKED) {
		unsigned long locked, lock_limit;
		locked = current->mm->locked_vm << PAGE_SHIFT;
		lock_limit = current->signal->rlim[RLIMIT_MEMLOCK].rlim_cur;
		locked += new_len - old_len;
		ret = -EAGAIN;
		if (locked > lock_limit && !capable(CAP_IPC_LOCK))
			goto out;
	}
	if (!may_expand_vm(current->mm, (new_len - old_len) >> PAGE_SHIFT)) {
		ret = -ENOMEM;
		goto out;
	}

	if (vma->vm_flags & VM_ACCOUNT) {
		charged = (new_len - old_len) >> PAGE_SHIFT;
		if (security_vm_enough_memory(charged))
			goto out_nc;
	}

	/* old_len exactly to the end of the area..
	 * And we're not relocating the area.
	 */
	if (old_len == vma->vm_end - addr &&
	    !((flags & MREMAP_FIXED) && (addr != new_addr)) &&
	    (old_len != new_len || !(flags & MREMAP_MAYMOVE))) {
		unsigned long max_addr = TASK_SIZE;
		if (vma->vm_next)
			max_addr = vma->vm_next->vm_start;
		/* can we just expand the current mapping? */
		if (max_addr - addr >= new_len) {
			int pages = (new_len - old_len) >> PAGE_SHIFT;

			vma_adjust(vma, vma->vm_start,
				addr + new_len, vma->vm_pgoff, NULL);

			current->mm->total_vm += pages;
			__vm_stat_account(vma->vm_mm, vma->vm_flags,
							vma->vm_file, pages);
			if (vma->vm_flags & VM_LOCKED) {
				current->mm->locked_vm += pages;
				make_pages_present(addr + old_len,
						   addr + new_len);
			}
			ret = addr;
			goto out;
		}
	}

	/*
	 * We weren't able to just expand or shrink the area,
	 * we need to create a new one and move it..
	 */
	ret = -ENOMEM;
	if (flags & MREMAP_MAYMOVE) {
		if (!(flags & MREMAP_FIXED)) {
			unsigned long map_flags = 0;
			if (vma->vm_flags & VM_MAYSHARE)
				map_flags |= MAP_SHARED;

			new_addr = get_unmapped_area(vma->vm_file, 0, new_len,
						vma->vm_pgoff, map_flags);
			ret = new_addr;
			if (new_addr & ~PAGE_MASK)
				goto out;
		}
		ret = move_vma(vma, addr, old_len, new_len, new_addr);
	}
out:
	if (ret & ~PAGE_MASK)
		vm_unacct_memory(charged);
out_nc:
	return ret;
}

asmlinkage unsigned long sys_mremap(unsigned long addr,
	unsigned long old_len, unsigned long new_len,
	unsigned long flags, unsigned long new_addr)
{
	unsigned long ret;

	down_write(&current->mm->mmap_sem);
	ret = do_mremap(addr, old_len, new_len, flags, new_addr);
	up_write(&current->mm->mmap_sem);
	return ret;
}
