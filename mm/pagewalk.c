// SPDX-License-Identifier: GPL-2.0
#include <linux/pagewalk.h>
#include <linux/highmem.h>
#include <linux/sched.h>
#include <linux/hugetlb.h>
#include <linux/mmu_context.h>
#include <linux/swap.h>
#include <linux/swapops.h>

#include <asm/tlbflush.h>

#include "internal.h"

/*
 * We want to know the real level where a entry is located ignoring any
 * folding of levels which may be happening. For example if p4d is folded then
 * a missing entry found at level 1 (p4d) is actually at level 0 (pgd).
 */
static int real_depth(int depth)
{
	if (depth == 3 && PTRS_PER_PMD == 1)
		depth = 2;
	if (depth == 2 && PTRS_PER_PUD == 1)
		depth = 1;
	if (depth == 1 && PTRS_PER_P4D == 1)
		depth = 0;
	return depth;
}

static int walk_pte_range_inner(pte_t *pte, unsigned long addr,
				unsigned long end, struct mm_walk *walk)
{
	const struct mm_walk_ops *ops = walk->ops;
	int err = 0;

	for (;;) {
		if (ops->install_pte && pte_none(ptep_get(pte))) {
			pte_t new_pte;

			err = ops->install_pte(addr, addr + PAGE_SIZE, &new_pte,
					       walk);
			if (err)
				break;

			set_pte_at(walk->mm, addr, pte, new_pte);
			/* Non-present before, so for arches that need it. */
			if (!WARN_ON_ONCE(walk->no_vma))
				update_mmu_cache(walk->vma, addr, pte);
		} else {
			err = ops->pte_entry(pte, addr, addr + PAGE_SIZE, walk);
			if (err)
				break;
		}
		if (addr >= end - PAGE_SIZE)
			break;
		addr += PAGE_SIZE;
		pte++;
	}
	return err;
}

static int walk_pte_range(pmd_t *pmd, unsigned long addr, unsigned long end,
			  struct mm_walk *walk)
{
	pte_t *pte;
	int err = 0;
	spinlock_t *ptl;

	if (walk->no_vma) {
		/*
		 * pte_offset_map() might apply user-specific validation.
		 * Indeed, on x86_64 the pmd entries set up by init_espfix_ap()
		 * fit its pmd_bad() check (_PAGE_NX set and _PAGE_RW clear),
		 * and CONFIG_EFI_PGT_DUMP efi_mm goes so far as to walk them.
		 */
		if (walk->mm == &init_mm || addr >= TASK_SIZE)
			pte = pte_offset_kernel(pmd, addr);
		else
			pte = pte_offset_map(pmd, addr);
		if (pte) {
			err = walk_pte_range_inner(pte, addr, end, walk);
			if (walk->mm != &init_mm && addr < TASK_SIZE)
				pte_unmap(pte);
		}
	} else {
		pte = pte_offset_map_lock(walk->mm, pmd, addr, &ptl);
		if (pte) {
			err = walk_pte_range_inner(pte, addr, end, walk);
			pte_unmap_unlock(pte, ptl);
		}
	}
	if (!pte)
		walk->action = ACTION_AGAIN;
	return err;
}

static int walk_pmd_range(pud_t *pud, unsigned long addr, unsigned long end,
			  struct mm_walk *walk)
{
	pmd_t *pmd;
	unsigned long next;
	const struct mm_walk_ops *ops = walk->ops;
	bool has_handler = ops->pte_entry;
	bool has_install = ops->install_pte;
	int err = 0;
	int depth = real_depth(3);

	pmd = pmd_offset(pud, addr);
	do {
again:
		next = pmd_addr_end(addr, end);
		if (pmd_none(*pmd)) {
			if (has_install)
				err = __pte_alloc(walk->mm, pmd);
			else if (ops->pte_hole)
				err = ops->pte_hole(addr, next, depth, walk);
			if (err)
				break;
			if (!has_install)
				continue;
		}

		walk->action = ACTION_SUBTREE;

		/*
		 * This implies that each ->pmd_entry() handler
		 * needs to know about pmd_trans_huge() pmds
		 */
		if (ops->pmd_entry)
			err = ops->pmd_entry(pmd, addr, next, walk);
		if (err)
			break;

		if (walk->action == ACTION_AGAIN)
			goto again;
		if (walk->action == ACTION_CONTINUE)
			continue;

		if (!has_handler) { /* No handlers for lower page tables. */
			if (!has_install)
				continue; /* Nothing to do. */
			/*
			 * We are ONLY installing, so avoid unnecessarily
			 * splitting a present huge page.
			 */
			if (pmd_present(*pmd) && pmd_trans_huge(*pmd))
				continue;
		}

		if (walk->vma)
			split_huge_pmd(walk->vma, pmd, addr);
		else if (pmd_leaf(*pmd) || !pmd_present(*pmd))
			continue; /* Nothing to do. */

		err = walk_pte_range(pmd, addr, next, walk);
		if (err)
			break;

		if (walk->action == ACTION_AGAIN)
			goto again;

	} while (pmd++, addr = next, addr != end);

	return err;
}

static int walk_pud_range(p4d_t *p4d, unsigned long addr, unsigned long end,
			  struct mm_walk *walk)
{
	pud_t *pud;
	unsigned long next;
	const struct mm_walk_ops *ops = walk->ops;
	bool has_handler = ops->pmd_entry || ops->pte_entry;
	bool has_install = ops->install_pte;
	int err = 0;
	int depth = real_depth(2);

	pud = pud_offset(p4d, addr);
	do {
 again:
		next = pud_addr_end(addr, end);
		if (pud_none(*pud)) {
			if (has_install)
				err = __pmd_alloc(walk->mm, pud, addr);
			else if (ops->pte_hole)
				err = ops->pte_hole(addr, next, depth, walk);
			if (err)
				break;
			if (!has_install)
				continue;
		}

		walk->action = ACTION_SUBTREE;

		if (ops->pud_entry)
			err = ops->pud_entry(pud, addr, next, walk);
		if (err)
			break;

		if (walk->action == ACTION_AGAIN)
			goto again;
		if (walk->action == ACTION_CONTINUE)
			continue;

		if (!has_handler) { /* No handlers for lower page tables. */
			if (!has_install)
				continue; /* Nothing to do. */
			/*
			 * We are ONLY installing, so avoid unnecessarily
			 * splitting a present huge page.
			 */
			if (pud_present(*pud) && pud_trans_huge(*pud))
				continue;
		}

		if (walk->vma)
			split_huge_pud(walk->vma, pud, addr);
		else if (pud_leaf(*pud) || !pud_present(*pud))
			continue; /* Nothing to do. */

		if (pud_none(*pud))
			goto again;

		err = walk_pmd_range(pud, addr, next, walk);
		if (err)
			break;
	} while (pud++, addr = next, addr != end);

	return err;
}

static int walk_p4d_range(pgd_t *pgd, unsigned long addr, unsigned long end,
			  struct mm_walk *walk)
{
	p4d_t *p4d;
	unsigned long next;
	const struct mm_walk_ops *ops = walk->ops;
	bool has_handler = ops->pud_entry || ops->pmd_entry || ops->pte_entry;
	bool has_install = ops->install_pte;
	int err = 0;
	int depth = real_depth(1);

	p4d = p4d_offset(pgd, addr);
	do {
		next = p4d_addr_end(addr, end);
		if (p4d_none_or_clear_bad(p4d)) {
			if (has_install)
				err = __pud_alloc(walk->mm, p4d, addr);
			else if (ops->pte_hole)
				err = ops->pte_hole(addr, next, depth, walk);
			if (err)
				break;
			if (!has_install)
				continue;
		}
		if (ops->p4d_entry) {
			err = ops->p4d_entry(p4d, addr, next, walk);
			if (err)
				break;
		}
		if (has_handler || has_install)
			err = walk_pud_range(p4d, addr, next, walk);
		if (err)
			break;
	} while (p4d++, addr = next, addr != end);

	return err;
}

static int walk_pgd_range(unsigned long addr, unsigned long end,
			  struct mm_walk *walk)
{
	pgd_t *pgd;
	unsigned long next;
	const struct mm_walk_ops *ops = walk->ops;
	bool has_handler = ops->p4d_entry || ops->pud_entry || ops->pmd_entry ||
		ops->pte_entry;
	bool has_install = ops->install_pte;
	int err = 0;

	if (walk->pgd)
		pgd = walk->pgd + pgd_index(addr);
	else
		pgd = pgd_offset(walk->mm, addr);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(pgd)) {
			if (has_install)
				err = __p4d_alloc(walk->mm, pgd, addr);
			else if (ops->pte_hole)
				err = ops->pte_hole(addr, next, 0, walk);
			if (err)
				break;
			if (!has_install)
				continue;
		}
		if (ops->pgd_entry) {
			err = ops->pgd_entry(pgd, addr, next, walk);
			if (err)
				break;
		}
		if (has_handler || has_install)
			err = walk_p4d_range(pgd, addr, next, walk);
		if (err)
			break;
	} while (pgd++, addr = next, addr != end);

	return err;
}

#ifdef CONFIG_HUGETLB_PAGE
static unsigned long hugetlb_entry_end(struct hstate *h, unsigned long addr,
				       unsigned long end)
{
	unsigned long boundary = (addr & huge_page_mask(h)) + huge_page_size(h);
	return boundary < end ? boundary : end;
}

static int walk_hugetlb_range(unsigned long addr, unsigned long end,
			      struct mm_walk *walk)
{
	struct vm_area_struct *vma = walk->vma;
	struct hstate *h = hstate_vma(vma);
	unsigned long next;
	unsigned long hmask = huge_page_mask(h);
	unsigned long sz = huge_page_size(h);
	pte_t *pte;
	const struct mm_walk_ops *ops = walk->ops;
	int err = 0;

	hugetlb_vma_lock_read(vma);
	do {
		next = hugetlb_entry_end(h, addr, end);
		pte = hugetlb_walk(vma, addr & hmask, sz);
		if (pte)
			err = ops->hugetlb_entry(pte, hmask, addr, next, walk);
		else if (ops->pte_hole)
			err = ops->pte_hole(addr, next, -1, walk);
		if (err)
			break;
	} while (addr = next, addr != end);
	hugetlb_vma_unlock_read(vma);

	return err;
}

#else /* CONFIG_HUGETLB_PAGE */
static int walk_hugetlb_range(unsigned long addr, unsigned long end,
			      struct mm_walk *walk)
{
	return 0;
}

#endif /* CONFIG_HUGETLB_PAGE */

/*
 * Decide whether we really walk over the current vma on [@start, @end)
 * or skip it via the returned value. Return 0 if we do walk over the
 * current vma, and return 1 if we skip the vma. Negative values means
 * error, where we abort the current walk.
 */
static int walk_page_test(unsigned long start, unsigned long end,
			struct mm_walk *walk)
{
	struct vm_area_struct *vma = walk->vma;
	const struct mm_walk_ops *ops = walk->ops;

	if (ops->test_walk)
		return ops->test_walk(start, end, walk);

	/*
	 * vma(VM_PFNMAP) doesn't have any valid struct pages behind VM_PFNMAP
	 * range, so we don't walk over it as we do for normal vmas. However,
	 * Some callers are interested in handling hole range and they don't
	 * want to just ignore any single address range. Such users certainly
	 * define their ->pte_hole() callbacks, so let's delegate them to handle
	 * vma(VM_PFNMAP).
	 */
	if (vma->vm_flags & VM_PFNMAP) {
		int err = 1;
		if (ops->pte_hole)
			err = ops->pte_hole(start, end, -1, walk);
		return err ? err : 1;
	}
	return 0;
}

static int __walk_page_range(unsigned long start, unsigned long end,
			struct mm_walk *walk)
{
	int err = 0;
	struct vm_area_struct *vma = walk->vma;
	const struct mm_walk_ops *ops = walk->ops;
	bool is_hugetlb = is_vm_hugetlb_page(vma);

	/* We do not support hugetlb PTE installation. */
	if (ops->install_pte && is_hugetlb)
		return -EINVAL;

	if (ops->pre_vma) {
		err = ops->pre_vma(start, end, walk);
		if (err)
			return err;
	}

	if (is_hugetlb) {
		if (ops->hugetlb_entry)
			err = walk_hugetlb_range(start, end, walk);
	} else
		err = walk_pgd_range(start, end, walk);

	if (ops->post_vma)
		ops->post_vma(walk);

	return err;
}

static inline void process_mm_walk_lock(struct mm_struct *mm,
					enum page_walk_lock walk_lock)
{
	if (walk_lock == PGWALK_RDLOCK)
		mmap_assert_locked(mm);
	else if (walk_lock != PGWALK_VMA_RDLOCK_VERIFY)
		mmap_assert_write_locked(mm);
}

static inline void process_vma_walk_lock(struct vm_area_struct *vma,
					 enum page_walk_lock walk_lock)
{
#ifdef CONFIG_PER_VMA_LOCK
	switch (walk_lock) {
	case PGWALK_WRLOCK:
		vma_start_write(vma);
		break;
	case PGWALK_WRLOCK_VERIFY:
		vma_assert_write_locked(vma);
		break;
	case PGWALK_VMA_RDLOCK_VERIFY:
		vma_assert_locked(vma);
		break;
	case PGWALK_RDLOCK:
		/* PGWALK_RDLOCK is handled by process_mm_walk_lock */
		break;
	}
#endif
}

/*
 * See the comment for walk_page_range(), this performs the heavy lifting of the
 * operation, only sets no restrictions on how the walk proceeds.
 *
 * We usually restrict the ability to install PTEs, but this functionality is
 * available to internal memory management code and provided in mm/internal.h.
 */
int walk_page_range_mm(struct mm_struct *mm, unsigned long start,
		unsigned long end, const struct mm_walk_ops *ops,
		void *private)
{
	int err = 0;
	unsigned long next;
	struct vm_area_struct *vma;
	struct mm_walk walk = {
		.ops		= ops,
		.mm		= mm,
		.private	= private,
	};

	if (start >= end)
		return -EINVAL;

	if (!walk.mm)
		return -EINVAL;

	process_mm_walk_lock(walk.mm, ops->walk_lock);

	vma = find_vma(walk.mm, start);
	do {
		if (!vma) { /* after the last vma */
			walk.vma = NULL;
			next = end;
			if (ops->pte_hole)
				err = ops->pte_hole(start, next, -1, &walk);
		} else if (start < vma->vm_start) { /* outside vma */
			walk.vma = NULL;
			next = min(end, vma->vm_start);
			if (ops->pte_hole)
				err = ops->pte_hole(start, next, -1, &walk);
		} else { /* inside vma */
			process_vma_walk_lock(vma, ops->walk_lock);
			walk.vma = vma;
			next = min(end, vma->vm_end);
			vma = find_vma(mm, vma->vm_end);

			err = walk_page_test(start, next, &walk);
			if (err > 0) {
				/*
				 * positive return values are purely for
				 * controlling the pagewalk, so should never
				 * be passed to the callers.
				 */
				err = 0;
				continue;
			}
			if (err < 0)
				break;
			err = __walk_page_range(start, next, &walk);
		}
		if (err)
			break;
	} while (start = next, start < end);
	return err;
}

/*
 * Determine if the walk operations specified are permitted to be used for a
 * page table walk.
 *
 * This check is performed on all functions which are parameterised by walk
 * operations and exposed in include/linux/pagewalk.h.
 *
 * Internal memory management code can use the walk_page_range_mm() function to
 * be able to use all page walking operations.
 */
static bool check_ops_valid(const struct mm_walk_ops *ops)
{
	/*
	 * The installation of PTEs is solely under the control of memory
	 * management logic and subject to many subtle locking, security and
	 * cache considerations so we cannot permit other users to do so, and
	 * certainly not for exported symbols.
	 */
	if (ops->install_pte)
		return false;

	return true;
}

/**
 * walk_page_range - walk page table with caller specific callbacks
 * @mm:		mm_struct representing the target process of page table walk
 * @start:	start address of the virtual address range
 * @end:	end address of the virtual address range
 * @ops:	operation to call during the walk
 * @private:	private data for callbacks' usage
 *
 * Recursively walk the page table tree of the process represented by @mm
 * within the virtual address range [@start, @end). During walking, we can do
 * some caller-specific works for each entry, by setting up pmd_entry(),
 * pte_entry(), and/or hugetlb_entry(). If you don't set up for some of these
 * callbacks, the associated entries/pages are just ignored.
 * The return values of these callbacks are commonly defined like below:
 *
 *  - 0  : succeeded to handle the current entry, and if you don't reach the
 *         end address yet, continue to walk.
 *  - >0 : succeeded to handle the current entry, and return to the caller
 *         with caller specific value.
 *  - <0 : failed to handle the current entry, and return to the caller
 *         with error code.
 *
 * Before starting to walk page table, some callers want to check whether
 * they really want to walk over the current vma, typically by checking
 * its vm_flags. walk_page_test() and @ops->test_walk() are used for this
 * purpose.
 *
 * If operations need to be staged before and committed after a vma is walked,
 * there are two callbacks, pre_vma() and post_vma(). Note that post_vma(),
 * since it is intended to handle commit-type operations, can't return any
 * errors.
 *
 * struct mm_walk keeps current values of some common data like vma and pmd,
 * which are useful for the access from callbacks. If you want to pass some
 * caller-specific data to callbacks, @private should be helpful.
 *
 * Locking:
 *   Callers of walk_page_range() and walk_page_vma() should hold @mm->mmap_lock,
 *   because these function traverse vma list and/or access to vma's data.
 */
int walk_page_range(struct mm_struct *mm, unsigned long start,
		unsigned long end, const struct mm_walk_ops *ops,
		void *private)
{
	if (!check_ops_valid(ops))
		return -EINVAL;

	return walk_page_range_mm(mm, start, end, ops, private);
}

/**
 * walk_kernel_page_table_range - walk a range of kernel pagetables.
 * @start:	start address of the virtual address range
 * @end:	end address of the virtual address range
 * @ops:	operation to call during the walk
 * @pgd:	pgd to walk if different from mm->pgd
 * @private:	private data for callbacks' usage
 *
 * Similar to walk_page_range() but can walk any page tables even if they are
 * not backed by VMAs. Because 'unusual' entries may be walked this function
 * will also not lock the PTEs for the pte_entry() callback. This is useful for
 * walking kernel pages tables or page tables for firmware.
 *
 * Note: Be careful to walk the kernel pages tables, the caller may be need to
 * take other effective approaches (mmap lock may be insufficient) to prevent
 * the intermediate kernel page tables belonging to the specified address range
 * from being freed (e.g. memory hot-remove).
 */
int walk_kernel_page_table_range(unsigned long start, unsigned long end,
		const struct mm_walk_ops *ops, pgd_t *pgd, void *private)
{
	/*
	 * Kernel intermediate page tables are usually not freed, so the mmap
	 * read lock is sufficient. But there are some exceptions.
	 * E.g. memory hot-remove. In which case, the mmap lock is insufficient
	 * to prevent the intermediate kernel pages tables belonging to the
	 * specified address range from being freed. The caller should take
	 * other actions to prevent this race.
	 */
	mmap_assert_locked(&init_mm);

	return walk_kernel_page_table_range_lockless(start, end, ops, pgd,
						     private);
}

/*
 * Use this function to walk the kernel page tables locklessly. It should be
 * guaranteed that the caller has exclusive access over the range they are
 * operating on - that there should be no concurrent access, for example,
 * changing permissions for vmalloc objects.
 */
int walk_kernel_page_table_range_lockless(unsigned long start, unsigned long end,
		const struct mm_walk_ops *ops, pgd_t *pgd, void *private)
{
	struct mm_walk walk = {
		.ops		= ops,
		.mm		= &init_mm,
		.pgd		= pgd,
		.private	= private,
		.no_vma		= true
	};

	if (start >= end)
		return -EINVAL;
	if (!check_ops_valid(ops))
		return -EINVAL;

	return walk_pgd_range(start, end, &walk);
}

/**
 * walk_page_range_debug - walk a range of pagetables not backed by a vma
 * @mm:		mm_struct representing the target process of page table walk
 * @start:	start address of the virtual address range
 * @end:	end address of the virtual address range
 * @ops:	operation to call during the walk
 * @pgd:	pgd to walk if different from mm->pgd
 * @private:	private data for callbacks' usage
 *
 * Similar to walk_page_range() but can walk any page tables even if they are
 * not backed by VMAs. Because 'unusual' entries may be walked this function
 * will also not lock the PTEs for the pte_entry() callback.
 *
 * This is for debugging purposes ONLY.
 */
int walk_page_range_debug(struct mm_struct *mm, unsigned long start,
			  unsigned long end, const struct mm_walk_ops *ops,
			  pgd_t *pgd, void *private)
{
	struct mm_walk walk = {
		.ops		= ops,
		.mm		= mm,
		.pgd		= pgd,
		.private	= private,
		.no_vma		= true
	};

	/* For convenience, we allow traversal of kernel mappings. */
	if (mm == &init_mm)
		return walk_kernel_page_table_range(start, end, ops,
						    pgd, private);
	if (start >= end || !walk.mm)
		return -EINVAL;
	if (!check_ops_valid(ops))
		return -EINVAL;

	/*
	 * The mmap lock protects the page walker from changes to the page
	 * tables during the walk.  However a read lock is insufficient to
	 * protect those areas which don't have a VMA as munmap() detaches
	 * the VMAs before downgrading to a read lock and actually tearing
	 * down PTEs/page tables. In which case, the mmap write lock should
	 * be held.
	 */
	mmap_assert_write_locked(mm);

	return walk_pgd_range(start, end, &walk);
}

int walk_page_range_vma(struct vm_area_struct *vma, unsigned long start,
			unsigned long end, const struct mm_walk_ops *ops,
			void *private)
{
	struct mm_walk walk = {
		.ops		= ops,
		.mm		= vma->vm_mm,
		.vma		= vma,
		.private	= private,
	};

	if (start >= end || !walk.mm)
		return -EINVAL;
	if (start < vma->vm_start || end > vma->vm_end)
		return -EINVAL;
	if (!check_ops_valid(ops))
		return -EINVAL;

	process_mm_walk_lock(walk.mm, ops->walk_lock);
	process_vma_walk_lock(vma, ops->walk_lock);
	return __walk_page_range(start, end, &walk);
}

int walk_page_vma(struct vm_area_struct *vma, const struct mm_walk_ops *ops,
		void *private)
{
	struct mm_walk walk = {
		.ops		= ops,
		.mm		= vma->vm_mm,
		.vma		= vma,
		.private	= private,
	};

	if (!walk.mm)
		return -EINVAL;
	if (!check_ops_valid(ops))
		return -EINVAL;

	process_mm_walk_lock(walk.mm, ops->walk_lock);
	process_vma_walk_lock(vma, ops->walk_lock);
	return __walk_page_range(vma->vm_start, vma->vm_end, &walk);
}

/**
 * walk_page_mapping - walk all memory areas mapped into a struct address_space.
 * @mapping: Pointer to the struct address_space
 * @first_index: First page offset in the address_space
 * @nr: Number of incremental page offsets to cover
 * @ops:	operation to call during the walk
 * @private:	private data for callbacks' usage
 *
 * This function walks all memory areas mapped into a struct address_space.
 * The walk is limited to only the given page-size index range, but if
 * the index boundaries cross a huge page-table entry, that entry will be
 * included.
 *
 * Also see walk_page_range() for additional information.
 *
 * Locking:
 *   This function can't require that the struct mm_struct::mmap_lock is held,
 *   since @mapping may be mapped by multiple processes. Instead
 *   @mapping->i_mmap_rwsem must be held. This might have implications in the
 *   callbacks, and it's up tho the caller to ensure that the
 *   struct mm_struct::mmap_lock is not needed.
 *
 *   Also this means that a caller can't rely on the struct
 *   vm_area_struct::vm_flags to be constant across a call,
 *   except for immutable flags. Callers requiring this shouldn't use
 *   this function.
 *
 * Return: 0 on success, negative error code on failure, positive number on
 * caller defined premature termination.
 */
int walk_page_mapping(struct address_space *mapping, pgoff_t first_index,
		      pgoff_t nr, const struct mm_walk_ops *ops,
		      void *private)
{
	struct mm_walk walk = {
		.ops		= ops,
		.private	= private,
	};
	struct vm_area_struct *vma;
	pgoff_t vba, vea, cba, cea;
	unsigned long start_addr, end_addr;
	int err = 0;

	if (!check_ops_valid(ops))
		return -EINVAL;

	lockdep_assert_held(&mapping->i_mmap_rwsem);
	vma_interval_tree_foreach(vma, &mapping->i_mmap, first_index,
				  first_index + nr - 1) {
		/* Clip to the vma */
		vba = vma->vm_pgoff;
		vea = vba + vma_pages(vma);
		cba = first_index;
		cba = max(cba, vba);
		cea = first_index + nr;
		cea = min(cea, vea);

		start_addr = ((cba - vba) << PAGE_SHIFT) + vma->vm_start;
		end_addr = ((cea - vba) << PAGE_SHIFT) + vma->vm_start;
		if (start_addr >= end_addr)
			continue;

		walk.vma = vma;
		walk.mm = vma->vm_mm;

		err = walk_page_test(vma->vm_start, vma->vm_end, &walk);
		if (err > 0) {
			err = 0;
			break;
		} else if (err < 0)
			break;

		err = __walk_page_range(start_addr, end_addr, &walk);
		if (err)
			break;
	}

	return err;
}

/**
 * folio_walk_start - walk the page tables to a folio
 * @fw: filled with information on success.
 * @vma: the VMA.
 * @addr: the virtual address to use for the page table walk.
 * @flags: flags modifying which folios to walk to.
 *
 * Walk the page tables using @addr in a given @vma to a mapped folio and
 * return the folio, making sure that the page table entry referenced by
 * @addr cannot change until folio_walk_end() was called.
 *
 * As default, this function returns only folios that are not special (e.g., not
 * the zeropage) and never returns folios that are supposed to be ignored by the
 * VM as documented by vm_normal_page(). If requested, zeropages will be
 * returned as well.
 *
 * As default, this function only considers present page table entries.
 * If requested, it will also consider migration entries.
 *
 * If this function returns NULL it might either indicate "there is nothing" or
 * "there is nothing suitable".
 *
 * On success, @fw is filled and the function returns the folio while the PTL
 * is still held and folio_walk_end() must be called to clean up,
 * releasing any held locks. The returned folio must *not* be used after the
 * call to folio_walk_end(), unless a short-term folio reference is taken before
 * that call.
 *
 * @fw->page will correspond to the page that is effectively referenced by
 * @addr. However, for migration entries and shared zeropages @fw->page is
 * set to NULL. Note that large folios might be mapped by multiple page table
 * entries, and this function will always only lookup a single entry as
 * specified by @addr, which might or might not cover more than a single page of
 * the returned folio.
 *
 * This function must *not* be used as a naive replacement for
 * get_user_pages() / pin_user_pages(), especially not to perform DMA or
 * to carelessly modify page content. This function may *only* be used to grab
 * short-term folio references, never to grab long-term folio references.
 *
 * Using the page table entry pointers in @fw for reading or modifying the
 * entry should be avoided where possible: however, there might be valid
 * use cases.
 *
 * WARNING: Modifying page table entries in hugetlb VMAs requires a lot of care.
 * For example, PMD page table sharing might require prior unsharing. Also,
 * logical hugetlb entries might span multiple physical page table entries,
 * which *must* be modified in a single operation (set_huge_pte_at(),
 * huge_ptep_set_*, ...). Note that the page table entry stored in @fw might
 * not correspond to the first physical entry of a logical hugetlb entry.
 *
 * The mmap lock must be held in read mode.
 *
 * Return: folio pointer on success, otherwise NULL.
 */
struct folio *folio_walk_start(struct folio_walk *fw,
		struct vm_area_struct *vma, unsigned long addr,
		folio_walk_flags_t flags)
{
	unsigned long entry_size;
	bool expose_page = true;
	struct page *page;
	pud_t *pudp, pud;
	pmd_t *pmdp, pmd;
	pte_t *ptep, pte;
	spinlock_t *ptl;
	pgd_t *pgdp;
	p4d_t *p4dp;

	mmap_assert_locked(vma->vm_mm);
	vma_pgtable_walk_begin(vma);

	if (WARN_ON_ONCE(addr < vma->vm_start || addr >= vma->vm_end))
		goto not_found;

	pgdp = pgd_offset(vma->vm_mm, addr);
	if (pgd_none_or_clear_bad(pgdp))
		goto not_found;

	p4dp = p4d_offset(pgdp, addr);
	if (p4d_none_or_clear_bad(p4dp))
		goto not_found;

	pudp = pud_offset(p4dp, addr);
	pud = pudp_get(pudp);
	if (pud_none(pud))
		goto not_found;
	if (IS_ENABLED(CONFIG_PGTABLE_HAS_HUGE_LEAVES) &&
	    (!pud_present(pud) || pud_leaf(pud))) {
		ptl = pud_lock(vma->vm_mm, pudp);
		pud = pudp_get(pudp);

		entry_size = PUD_SIZE;
		fw->level = FW_LEVEL_PUD;
		fw->pudp = pudp;
		fw->pud = pud;

		if (pud_none(pud)) {
			spin_unlock(ptl);
			goto not_found;
		} else if (pud_present(pud) && !pud_leaf(pud)) {
			spin_unlock(ptl);
			goto pmd_table;
		} else if (pud_present(pud)) {
			page = vm_normal_page_pud(vma, addr, pud);
			if (page)
				goto found;
		}
		/*
		 * TODO: FW_MIGRATION support for PUD migration entries
		 * once there are relevant users.
		 */
		spin_unlock(ptl);
		goto not_found;
	}

pmd_table:
	VM_WARN_ON_ONCE(!pud_present(pud) || pud_leaf(pud));
	pmdp = pmd_offset(pudp, addr);
	pmd = pmdp_get_lockless(pmdp);
	if (pmd_none(pmd))
		goto not_found;
	if (IS_ENABLED(CONFIG_PGTABLE_HAS_HUGE_LEAVES) &&
	    (!pmd_present(pmd) || pmd_leaf(pmd))) {
		ptl = pmd_lock(vma->vm_mm, pmdp);
		pmd = pmdp_get(pmdp);

		entry_size = PMD_SIZE;
		fw->level = FW_LEVEL_PMD;
		fw->pmdp = pmdp;
		fw->pmd = pmd;

		if (pmd_none(pmd)) {
			spin_unlock(ptl);
			goto not_found;
		} else if (pmd_present(pmd) && !pmd_leaf(pmd)) {
			spin_unlock(ptl);
			goto pte_table;
		} else if (pmd_present(pmd)) {
			page = vm_normal_page_pmd(vma, addr, pmd);
			if (page) {
				goto found;
			} else if ((flags & FW_ZEROPAGE) &&
				    is_huge_zero_pmd(pmd)) {
				page = pfn_to_page(pmd_pfn(pmd));
				expose_page = false;
				goto found;
			}
		} else if ((flags & FW_MIGRATION) &&
			   is_pmd_migration_entry(pmd)) {
			swp_entry_t entry = pmd_to_swp_entry(pmd);

			page = pfn_swap_entry_to_page(entry);
			expose_page = false;
			goto found;
		}
		spin_unlock(ptl);
		goto not_found;
	}

pte_table:
	VM_WARN_ON_ONCE(!pmd_present(pmd) || pmd_leaf(pmd));
	ptep = pte_offset_map_lock(vma->vm_mm, pmdp, addr, &ptl);
	if (!ptep)
		goto not_found;
	pte = ptep_get(ptep);

	entry_size = PAGE_SIZE;
	fw->level = FW_LEVEL_PTE;
	fw->ptep = ptep;
	fw->pte = pte;

	if (pte_present(pte)) {
		page = vm_normal_page(vma, addr, pte);
		if (page)
			goto found;
		if ((flags & FW_ZEROPAGE) &&
		    is_zero_pfn(pte_pfn(pte))) {
			page = pfn_to_page(pte_pfn(pte));
			expose_page = false;
			goto found;
		}
	} else if (!pte_none(pte)) {
		swp_entry_t entry = pte_to_swp_entry(pte);

		if ((flags & FW_MIGRATION) &&
		    is_migration_entry(entry)) {
			page = pfn_swap_entry_to_page(entry);
			expose_page = false;
			goto found;
		}
	}
	pte_unmap_unlock(ptep, ptl);
not_found:
	vma_pgtable_walk_end(vma);
	return NULL;
found:
	if (expose_page)
		/* Note: Offset from the mapped page, not the folio start. */
		fw->page = page + ((addr & (entry_size - 1)) >> PAGE_SHIFT);
	else
		fw->page = NULL;
	fw->ptl = ptl;
	return page_folio(page);
}
