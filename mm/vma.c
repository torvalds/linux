// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * VMA-specific functions.
 */

#include "vma_internal.h"
#include "vma.h"

/*
 * If the vma has a ->close operation then the driver probably needs to release
 * per-vma resources, so we don't attempt to merge those if the caller indicates
 * the current vma may be removed as part of the merge.
 */
static inline bool is_mergeable_vma(struct vm_area_struct *vma,
		struct file *file, unsigned long vm_flags,
		struct vm_userfaultfd_ctx vm_userfaultfd_ctx,
		struct anon_vma_name *anon_name, bool may_remove_vma)
{
	/*
	 * VM_SOFTDIRTY should not prevent from VMA merging, if we
	 * match the flags but dirty bit -- the caller should mark
	 * merged VMA as dirty. If dirty bit won't be excluded from
	 * comparison, we increase pressure on the memory system forcing
	 * the kernel to generate new VMAs when old one could be
	 * extended instead.
	 */
	if ((vma->vm_flags ^ vm_flags) & ~VM_SOFTDIRTY)
		return false;
	if (vma->vm_file != file)
		return false;
	if (may_remove_vma && vma->vm_ops && vma->vm_ops->close)
		return false;
	if (!is_mergeable_vm_userfaultfd_ctx(vma, vm_userfaultfd_ctx))
		return false;
	if (!anon_vma_name_eq(anon_vma_name(vma), anon_name))
		return false;
	return true;
}

static inline bool is_mergeable_anon_vma(struct anon_vma *anon_vma1,
		 struct anon_vma *anon_vma2, struct vm_area_struct *vma)
{
	/*
	 * The list_is_singular() test is to avoid merging VMA cloned from
	 * parents. This can improve scalability caused by anon_vma lock.
	 */
	if ((!anon_vma1 || !anon_vma2) && (!vma ||
		list_is_singular(&vma->anon_vma_chain)))
		return true;
	return anon_vma1 == anon_vma2;
}

/*
 * init_multi_vma_prep() - Initializer for struct vma_prepare
 * @vp: The vma_prepare struct
 * @vma: The vma that will be altered once locked
 * @next: The next vma if it is to be adjusted
 * @remove: The first vma to be removed
 * @remove2: The second vma to be removed
 */
static void init_multi_vma_prep(struct vma_prepare *vp,
				struct vm_area_struct *vma,
				struct vm_area_struct *next,
				struct vm_area_struct *remove,
				struct vm_area_struct *remove2)
{
	memset(vp, 0, sizeof(struct vma_prepare));
	vp->vma = vma;
	vp->anon_vma = vma->anon_vma;
	vp->remove = remove;
	vp->remove2 = remove2;
	vp->adj_next = next;
	if (!vp->anon_vma && next)
		vp->anon_vma = next->anon_vma;

	vp->file = vma->vm_file;
	if (vp->file)
		vp->mapping = vma->vm_file->f_mapping;

}

/*
 * Return true if we can merge this (vm_flags,anon_vma,file,vm_pgoff)
 * in front of (at a lower virtual address and file offset than) the vma.
 *
 * We cannot merge two vmas if they have differently assigned (non-NULL)
 * anon_vmas, nor if same anon_vma is assigned but offsets incompatible.
 *
 * We don't check here for the merged mmap wrapping around the end of pagecache
 * indices (16TB on ia32) because do_mmap() does not permit mmap's which
 * wrap, nor mmaps which cover the final page at index -1UL.
 *
 * We assume the vma may be removed as part of the merge.
 */
bool
can_vma_merge_before(struct vm_area_struct *vma, unsigned long vm_flags,
		struct anon_vma *anon_vma, struct file *file,
		pgoff_t vm_pgoff, struct vm_userfaultfd_ctx vm_userfaultfd_ctx,
		struct anon_vma_name *anon_name)
{
	if (is_mergeable_vma(vma, file, vm_flags, vm_userfaultfd_ctx, anon_name, true) &&
	    is_mergeable_anon_vma(anon_vma, vma->anon_vma, vma)) {
		if (vma->vm_pgoff == vm_pgoff)
			return true;
	}
	return false;
}

/*
 * Return true if we can merge this (vm_flags,anon_vma,file,vm_pgoff)
 * beyond (at a higher virtual address and file offset than) the vma.
 *
 * We cannot merge two vmas if they have differently assigned (non-NULL)
 * anon_vmas, nor if same anon_vma is assigned but offsets incompatible.
 *
 * We assume that vma is not removed as part of the merge.
 */
bool
can_vma_merge_after(struct vm_area_struct *vma, unsigned long vm_flags,
		struct anon_vma *anon_vma, struct file *file,
		pgoff_t vm_pgoff, struct vm_userfaultfd_ctx vm_userfaultfd_ctx,
		struct anon_vma_name *anon_name)
{
	if (is_mergeable_vma(vma, file, vm_flags, vm_userfaultfd_ctx, anon_name, false) &&
	    is_mergeable_anon_vma(anon_vma, vma->anon_vma, vma)) {
		pgoff_t vm_pglen;

		vm_pglen = vma_pages(vma);
		if (vma->vm_pgoff + vm_pglen == vm_pgoff)
			return true;
	}
	return false;
}

/*
 * Close a vm structure and free it.
 */
void remove_vma(struct vm_area_struct *vma, bool unreachable)
{
	might_sleep();
	if (vma->vm_ops && vma->vm_ops->close)
		vma->vm_ops->close(vma);
	if (vma->vm_file)
		fput(vma->vm_file);
	mpol_put(vma_policy(vma));
	if (unreachable)
		__vm_area_free(vma);
	else
		vm_area_free(vma);
}

/*
 * Get rid of page table information in the indicated region.
 *
 * Called with the mm semaphore held.
 */
void unmap_region(struct mm_struct *mm, struct ma_state *mas,
		struct vm_area_struct *vma, struct vm_area_struct *prev,
		struct vm_area_struct *next, unsigned long start,
		unsigned long end, unsigned long tree_end, bool mm_wr_locked)
{
	struct mmu_gather tlb;
	unsigned long mt_start = mas->index;

	lru_add_drain();
	tlb_gather_mmu(&tlb, mm);
	update_hiwater_rss(mm);
	unmap_vmas(&tlb, mas, vma, start, end, tree_end, mm_wr_locked);
	mas_set(mas, mt_start);
	free_pgtables(&tlb, mas, vma, prev ? prev->vm_end : FIRST_USER_ADDRESS,
				 next ? next->vm_start : USER_PGTABLES_CEILING,
				 mm_wr_locked);
	tlb_finish_mmu(&tlb);
}

/*
 * __split_vma() bypasses sysctl_max_map_count checking.  We use this where it
 * has already been checked or doesn't make sense to fail.
 * VMA Iterator will point to the original VMA.
 */
static int __split_vma(struct vma_iterator *vmi, struct vm_area_struct *vma,
		       unsigned long addr, int new_below)
{
	struct vma_prepare vp;
	struct vm_area_struct *new;
	int err;

	WARN_ON(vma->vm_start >= addr);
	WARN_ON(vma->vm_end <= addr);

	if (vma->vm_ops && vma->vm_ops->may_split) {
		err = vma->vm_ops->may_split(vma, addr);
		if (err)
			return err;
	}

	new = vm_area_dup(vma);
	if (!new)
		return -ENOMEM;

	if (new_below) {
		new->vm_end = addr;
	} else {
		new->vm_start = addr;
		new->vm_pgoff += ((addr - vma->vm_start) >> PAGE_SHIFT);
	}

	err = -ENOMEM;
	vma_iter_config(vmi, new->vm_start, new->vm_end);
	if (vma_iter_prealloc(vmi, new))
		goto out_free_vma;

	err = vma_dup_policy(vma, new);
	if (err)
		goto out_free_vmi;

	err = anon_vma_clone(new, vma);
	if (err)
		goto out_free_mpol;

	if (new->vm_file)
		get_file(new->vm_file);

	if (new->vm_ops && new->vm_ops->open)
		new->vm_ops->open(new);

	vma_start_write(vma);
	vma_start_write(new);

	init_vma_prep(&vp, vma);
	vp.insert = new;
	vma_prepare(&vp);
	vma_adjust_trans_huge(vma, vma->vm_start, addr, 0);

	if (new_below) {
		vma->vm_start = addr;
		vma->vm_pgoff += (addr - new->vm_start) >> PAGE_SHIFT;
	} else {
		vma->vm_end = addr;
	}

	/* vma_complete stores the new vma */
	vma_complete(&vp, vmi, vma->vm_mm);

	/* Success. */
	if (new_below)
		vma_next(vmi);
	else
		vma_prev(vmi);

	return 0;

out_free_mpol:
	mpol_put(vma_policy(new));
out_free_vmi:
	vma_iter_free(vmi);
out_free_vma:
	vm_area_free(new);
	return err;
}

/*
 * Split a vma into two pieces at address 'addr', a new vma is allocated
 * either for the first part or the tail.
 */
static int split_vma(struct vma_iterator *vmi, struct vm_area_struct *vma,
		     unsigned long addr, int new_below)
{
	if (vma->vm_mm->map_count >= sysctl_max_map_count)
		return -ENOMEM;

	return __split_vma(vmi, vma, addr, new_below);
}

/*
 * Ok - we have the memory areas we should free on a maple tree so release them,
 * and do the vma updates.
 *
 * Called with the mm semaphore held.
 */
static inline void remove_mt(struct mm_struct *mm, struct ma_state *mas)
{
	unsigned long nr_accounted = 0;
	struct vm_area_struct *vma;

	/* Update high watermark before we lower total_vm */
	update_hiwater_vm(mm);
	mas_for_each(mas, vma, ULONG_MAX) {
		long nrpages = vma_pages(vma);

		if (vma->vm_flags & VM_ACCOUNT)
			nr_accounted += nrpages;
		vm_stat_account(mm, vma->vm_flags, -nrpages);
		remove_vma(vma, false);
	}
	vm_unacct_memory(nr_accounted);
}

/*
 * init_vma_prep() - Initializer wrapper for vma_prepare struct
 * @vp: The vma_prepare struct
 * @vma: The vma that will be altered once locked
 */
void init_vma_prep(struct vma_prepare *vp,
		   struct vm_area_struct *vma)
{
	init_multi_vma_prep(vp, vma, NULL, NULL, NULL);
}

/*
 * Requires inode->i_mapping->i_mmap_rwsem
 */
static void __remove_shared_vm_struct(struct vm_area_struct *vma,
				      struct address_space *mapping)
{
	if (vma_is_shared_maywrite(vma))
		mapping_unmap_writable(mapping);

	flush_dcache_mmap_lock(mapping);
	vma_interval_tree_remove(vma, &mapping->i_mmap);
	flush_dcache_mmap_unlock(mapping);
}

/*
 * vma has some anon_vma assigned, and is already inserted on that
 * anon_vma's interval trees.
 *
 * Before updating the vma's vm_start / vm_end / vm_pgoff fields, the
 * vma must be removed from the anon_vma's interval trees using
 * anon_vma_interval_tree_pre_update_vma().
 *
 * After the update, the vma will be reinserted using
 * anon_vma_interval_tree_post_update_vma().
 *
 * The entire update must be protected by exclusive mmap_lock and by
 * the root anon_vma's mutex.
 */
void
anon_vma_interval_tree_pre_update_vma(struct vm_area_struct *vma)
{
	struct anon_vma_chain *avc;

	list_for_each_entry(avc, &vma->anon_vma_chain, same_vma)
		anon_vma_interval_tree_remove(avc, &avc->anon_vma->rb_root);
}

void
anon_vma_interval_tree_post_update_vma(struct vm_area_struct *vma)
{
	struct anon_vma_chain *avc;

	list_for_each_entry(avc, &vma->anon_vma_chain, same_vma)
		anon_vma_interval_tree_insert(avc, &avc->anon_vma->rb_root);
}

static void __vma_link_file(struct vm_area_struct *vma,
			    struct address_space *mapping)
{
	if (vma_is_shared_maywrite(vma))
		mapping_allow_writable(mapping);

	flush_dcache_mmap_lock(mapping);
	vma_interval_tree_insert(vma, &mapping->i_mmap);
	flush_dcache_mmap_unlock(mapping);
}

/*
 * vma_prepare() - Helper function for handling locking VMAs prior to altering
 * @vp: The initialized vma_prepare struct
 */
void vma_prepare(struct vma_prepare *vp)
{
	if (vp->file) {
		uprobe_munmap(vp->vma, vp->vma->vm_start, vp->vma->vm_end);

		if (vp->adj_next)
			uprobe_munmap(vp->adj_next, vp->adj_next->vm_start,
				      vp->adj_next->vm_end);

		i_mmap_lock_write(vp->mapping);
		if (vp->insert && vp->insert->vm_file) {
			/*
			 * Put into interval tree now, so instantiated pages
			 * are visible to arm/parisc __flush_dcache_page
			 * throughout; but we cannot insert into address
			 * space until vma start or end is updated.
			 */
			__vma_link_file(vp->insert,
					vp->insert->vm_file->f_mapping);
		}
	}

	if (vp->anon_vma) {
		anon_vma_lock_write(vp->anon_vma);
		anon_vma_interval_tree_pre_update_vma(vp->vma);
		if (vp->adj_next)
			anon_vma_interval_tree_pre_update_vma(vp->adj_next);
	}

	if (vp->file) {
		flush_dcache_mmap_lock(vp->mapping);
		vma_interval_tree_remove(vp->vma, &vp->mapping->i_mmap);
		if (vp->adj_next)
			vma_interval_tree_remove(vp->adj_next,
						 &vp->mapping->i_mmap);
	}

}

/*
 * dup_anon_vma() - Helper function to duplicate anon_vma
 * @dst: The destination VMA
 * @src: The source VMA
 * @dup: Pointer to the destination VMA when successful.
 *
 * Returns: 0 on success.
 */
static int dup_anon_vma(struct vm_area_struct *dst,
			struct vm_area_struct *src, struct vm_area_struct **dup)
{
	/*
	 * Easily overlooked: when mprotect shifts the boundary, make sure the
	 * expanding vma has anon_vma set if the shrinking vma had, to cover any
	 * anon pages imported.
	 */
	if (src->anon_vma && !dst->anon_vma) {
		int ret;

		vma_assert_write_locked(dst);
		dst->anon_vma = src->anon_vma;
		ret = anon_vma_clone(dst, src);
		if (ret)
			return ret;

		*dup = dst;
	}

	return 0;
}

#ifdef CONFIG_DEBUG_VM_MAPLE_TREE
void validate_mm(struct mm_struct *mm)
{
	int bug = 0;
	int i = 0;
	struct vm_area_struct *vma;
	VMA_ITERATOR(vmi, mm, 0);

	mt_validate(&mm->mm_mt);
	for_each_vma(vmi, vma) {
#ifdef CONFIG_DEBUG_VM_RB
		struct anon_vma *anon_vma = vma->anon_vma;
		struct anon_vma_chain *avc;
#endif
		unsigned long vmi_start, vmi_end;
		bool warn = 0;

		vmi_start = vma_iter_addr(&vmi);
		vmi_end = vma_iter_end(&vmi);
		if (VM_WARN_ON_ONCE_MM(vma->vm_end != vmi_end, mm))
			warn = 1;

		if (VM_WARN_ON_ONCE_MM(vma->vm_start != vmi_start, mm))
			warn = 1;

		if (warn) {
			pr_emerg("issue in %s\n", current->comm);
			dump_stack();
			dump_vma(vma);
			pr_emerg("tree range: %px start %lx end %lx\n", vma,
				 vmi_start, vmi_end - 1);
			vma_iter_dump_tree(&vmi);
		}

#ifdef CONFIG_DEBUG_VM_RB
		if (anon_vma) {
			anon_vma_lock_read(anon_vma);
			list_for_each_entry(avc, &vma->anon_vma_chain, same_vma)
				anon_vma_interval_tree_verify(avc);
			anon_vma_unlock_read(anon_vma);
		}
#endif
		i++;
	}
	if (i != mm->map_count) {
		pr_emerg("map_count %d vma iterator %d\n", mm->map_count, i);
		bug = 1;
	}
	VM_BUG_ON_MM(bug, mm);
}
#endif /* CONFIG_DEBUG_VM_MAPLE_TREE */

/*
 * vma_expand - Expand an existing VMA
 *
 * @vmi: The vma iterator
 * @vma: The vma to expand
 * @start: The start of the vma
 * @end: The exclusive end of the vma
 * @pgoff: The page offset of vma
 * @next: The current of next vma.
 *
 * Expand @vma to @start and @end.  Can expand off the start and end.  Will
 * expand over @next if it's different from @vma and @end == @next->vm_end.
 * Checking if the @vma can expand and merge with @next needs to be handled by
 * the caller.
 *
 * Returns: 0 on success
 */
int vma_expand(struct vma_iterator *vmi, struct vm_area_struct *vma,
	       unsigned long start, unsigned long end, pgoff_t pgoff,
	       struct vm_area_struct *next)
{
	struct vm_area_struct *anon_dup = NULL;
	bool remove_next = false;
	struct vma_prepare vp;

	vma_start_write(vma);
	if (next && (vma != next) && (end == next->vm_end)) {
		int ret;

		remove_next = true;
		vma_start_write(next);
		ret = dup_anon_vma(vma, next, &anon_dup);
		if (ret)
			return ret;
	}

	init_multi_vma_prep(&vp, vma, NULL, remove_next ? next : NULL, NULL);
	/* Not merging but overwriting any part of next is not handled. */
	VM_WARN_ON(next && !vp.remove &&
		  next != vma && end > next->vm_start);
	/* Only handles expanding */
	VM_WARN_ON(vma->vm_start < start || vma->vm_end > end);

	/* Note: vma iterator must be pointing to 'start' */
	vma_iter_config(vmi, start, end);
	if (vma_iter_prealloc(vmi, vma))
		goto nomem;

	vma_prepare(&vp);
	vma_adjust_trans_huge(vma, start, end, 0);
	vma_set_range(vma, start, end, pgoff);
	vma_iter_store(vmi, vma);

	vma_complete(&vp, vmi, vma->vm_mm);
	return 0;

nomem:
	if (anon_dup)
		unlink_anon_vmas(anon_dup);
	return -ENOMEM;
}

/*
 * vma_shrink() - Reduce an existing VMAs memory area
 * @vmi: The vma iterator
 * @vma: The VMA to modify
 * @start: The new start
 * @end: The new end
 *
 * Returns: 0 on success, -ENOMEM otherwise
 */
int vma_shrink(struct vma_iterator *vmi, struct vm_area_struct *vma,
	       unsigned long start, unsigned long end, pgoff_t pgoff)
{
	struct vma_prepare vp;

	WARN_ON((vma->vm_start != start) && (vma->vm_end != end));

	if (vma->vm_start < start)
		vma_iter_config(vmi, vma->vm_start, start);
	else
		vma_iter_config(vmi, end, vma->vm_end);

	if (vma_iter_prealloc(vmi, NULL))
		return -ENOMEM;

	vma_start_write(vma);

	init_vma_prep(&vp, vma);
	vma_prepare(&vp);
	vma_adjust_trans_huge(vma, start, end, 0);

	vma_iter_clear(vmi);
	vma_set_range(vma, start, end, pgoff);
	vma_complete(&vp, vmi, vma->vm_mm);
	return 0;
}

/*
 * vma_complete- Helper function for handling the unlocking after altering VMAs,
 * or for inserting a VMA.
 *
 * @vp: The vma_prepare struct
 * @vmi: The vma iterator
 * @mm: The mm_struct
 */
void vma_complete(struct vma_prepare *vp,
		  struct vma_iterator *vmi, struct mm_struct *mm)
{
	if (vp->file) {
		if (vp->adj_next)
			vma_interval_tree_insert(vp->adj_next,
						 &vp->mapping->i_mmap);
		vma_interval_tree_insert(vp->vma, &vp->mapping->i_mmap);
		flush_dcache_mmap_unlock(vp->mapping);
	}

	if (vp->remove && vp->file) {
		__remove_shared_vm_struct(vp->remove, vp->mapping);
		if (vp->remove2)
			__remove_shared_vm_struct(vp->remove2, vp->mapping);
	} else if (vp->insert) {
		/*
		 * split_vma has split insert from vma, and needs
		 * us to insert it before dropping the locks
		 * (it may either follow vma or precede it).
		 */
		vma_iter_store(vmi, vp->insert);
		mm->map_count++;
	}

	if (vp->anon_vma) {
		anon_vma_interval_tree_post_update_vma(vp->vma);
		if (vp->adj_next)
			anon_vma_interval_tree_post_update_vma(vp->adj_next);
		anon_vma_unlock_write(vp->anon_vma);
	}

	if (vp->file) {
		i_mmap_unlock_write(vp->mapping);
		uprobe_mmap(vp->vma);

		if (vp->adj_next)
			uprobe_mmap(vp->adj_next);
	}

	if (vp->remove) {
again:
		vma_mark_detached(vp->remove, true);
		if (vp->file) {
			uprobe_munmap(vp->remove, vp->remove->vm_start,
				      vp->remove->vm_end);
			fput(vp->file);
		}
		if (vp->remove->anon_vma)
			anon_vma_merge(vp->vma, vp->remove);
		mm->map_count--;
		mpol_put(vma_policy(vp->remove));
		if (!vp->remove2)
			WARN_ON_ONCE(vp->vma->vm_end < vp->remove->vm_end);
		vm_area_free(vp->remove);

		/*
		 * In mprotect's case 6 (see comments on vma_merge),
		 * we are removing both mid and next vmas
		 */
		if (vp->remove2) {
			vp->remove = vp->remove2;
			vp->remove2 = NULL;
			goto again;
		}
	}
	if (vp->insert && vp->file)
		uprobe_mmap(vp->insert);
	validate_mm(mm);
}

/*
 * abort_munmap_vmas - Undo any munmap work and free resources
 *
 * Reattach any detached vmas and free up the maple tree used to track the vmas.
 */
static inline void abort_munmap_vmas(struct ma_state *mas_detach)
{
	struct vm_area_struct *vma;

	mas_set(mas_detach, 0);
	mas_for_each(mas_detach, vma, ULONG_MAX)
		vma_mark_detached(vma, false);

	__mt_destroy(mas_detach->tree);
}

/*
 * do_vmi_align_munmap() - munmap the aligned region from @start to @end.
 * @vmi: The vma iterator
 * @vma: The starting vm_area_struct
 * @mm: The mm_struct
 * @start: The aligned start address to munmap.
 * @end: The aligned end address to munmap.
 * @uf: The userfaultfd list_head
 * @unlock: Set to true to drop the mmap_lock.  unlocking only happens on
 * success.
 *
 * Return: 0 on success and drops the lock if so directed, error and leaves the
 * lock held otherwise.
 */
int
do_vmi_align_munmap(struct vma_iterator *vmi, struct vm_area_struct *vma,
		    struct mm_struct *mm, unsigned long start,
		    unsigned long end, struct list_head *uf, bool unlock)
{
	struct vm_area_struct *prev, *next = NULL;
	struct maple_tree mt_detach;
	int count = 0;
	int error = -ENOMEM;
	unsigned long locked_vm = 0;
	MA_STATE(mas_detach, &mt_detach, 0, 0);
	mt_init_flags(&mt_detach, vmi->mas.tree->ma_flags & MT_FLAGS_LOCK_MASK);
	mt_on_stack(mt_detach);

	/*
	 * If we need to split any vma, do it now to save pain later.
	 *
	 * Note: mremap's move_vma VM_ACCOUNT handling assumes a partially
	 * unmapped vm_area_struct will remain in use: so lower split_vma
	 * places tmp vma above, and higher split_vma places tmp vma below.
	 */

	/* Does it split the first one? */
	if (start > vma->vm_start) {

		/*
		 * Make sure that map_count on return from munmap() will
		 * not exceed its limit; but let map_count go just above
		 * its limit temporarily, to help free resources as expected.
		 */
		if (end < vma->vm_end && mm->map_count >= sysctl_max_map_count)
			goto map_count_exceeded;

		/* Don't bother splitting the VMA if we can't unmap it anyway */
		if (!can_modify_vma(vma)) {
			error = -EPERM;
			goto start_split_failed;
		}

		error = __split_vma(vmi, vma, start, 1);
		if (error)
			goto start_split_failed;
	}

	/*
	 * Detach a range of VMAs from the mm. Using next as a temp variable as
	 * it is always overwritten.
	 */
	next = vma;
	do {
		if (!can_modify_vma(next)) {
			error = -EPERM;
			goto modify_vma_failed;
		}

		/* Does it split the end? */
		if (next->vm_end > end) {
			error = __split_vma(vmi, next, end, 0);
			if (error)
				goto end_split_failed;
		}
		vma_start_write(next);
		mas_set(&mas_detach, count);
		error = mas_store_gfp(&mas_detach, next, GFP_KERNEL);
		if (error)
			goto munmap_gather_failed;
		vma_mark_detached(next, true);
		if (next->vm_flags & VM_LOCKED)
			locked_vm += vma_pages(next);

		count++;
		if (unlikely(uf)) {
			/*
			 * If userfaultfd_unmap_prep returns an error the vmas
			 * will remain split, but userland will get a
			 * highly unexpected error anyway. This is no
			 * different than the case where the first of the two
			 * __split_vma fails, but we don't undo the first
			 * split, despite we could. This is unlikely enough
			 * failure that it's not worth optimizing it for.
			 */
			error = userfaultfd_unmap_prep(next, start, end, uf);

			if (error)
				goto userfaultfd_error;
		}
#ifdef CONFIG_DEBUG_VM_MAPLE_TREE
		BUG_ON(next->vm_start < start);
		BUG_ON(next->vm_start > end);
#endif
	} for_each_vma_range(*vmi, next, end);

#if defined(CONFIG_DEBUG_VM_MAPLE_TREE)
	/* Make sure no VMAs are about to be lost. */
	{
		MA_STATE(test, &mt_detach, 0, 0);
		struct vm_area_struct *vma_mas, *vma_test;
		int test_count = 0;

		vma_iter_set(vmi, start);
		rcu_read_lock();
		vma_test = mas_find(&test, count - 1);
		for_each_vma_range(*vmi, vma_mas, end) {
			BUG_ON(vma_mas != vma_test);
			test_count++;
			vma_test = mas_next(&test, count - 1);
		}
		rcu_read_unlock();
		BUG_ON(count != test_count);
	}
#endif

	while (vma_iter_addr(vmi) > start)
		vma_iter_prev_range(vmi);

	error = vma_iter_clear_gfp(vmi, start, end, GFP_KERNEL);
	if (error)
		goto clear_tree_failed;

	/* Point of no return */
	mm->locked_vm -= locked_vm;
	mm->map_count -= count;
	if (unlock)
		mmap_write_downgrade(mm);

	prev = vma_iter_prev_range(vmi);
	next = vma_next(vmi);
	if (next)
		vma_iter_prev_range(vmi);

	/*
	 * We can free page tables without write-locking mmap_lock because VMAs
	 * were isolated before we downgraded mmap_lock.
	 */
	mas_set(&mas_detach, 1);
	unmap_region(mm, &mas_detach, vma, prev, next, start, end, count,
		     !unlock);
	/* Statistics and freeing VMAs */
	mas_set(&mas_detach, 0);
	remove_mt(mm, &mas_detach);
	validate_mm(mm);
	if (unlock)
		mmap_read_unlock(mm);

	__mt_destroy(&mt_detach);
	return 0;

modify_vma_failed:
clear_tree_failed:
userfaultfd_error:
munmap_gather_failed:
end_split_failed:
	abort_munmap_vmas(&mas_detach);
start_split_failed:
map_count_exceeded:
	validate_mm(mm);
	return error;
}

/*
 * do_vmi_munmap() - munmap a given range.
 * @vmi: The vma iterator
 * @mm: The mm_struct
 * @start: The start address to munmap
 * @len: The length of the range to munmap
 * @uf: The userfaultfd list_head
 * @unlock: set to true if the user wants to drop the mmap_lock on success
 *
 * This function takes a @mas that is either pointing to the previous VMA or set
 * to MA_START and sets it up to remove the mapping(s).  The @len will be
 * aligned.
 *
 * Return: 0 on success and drops the lock if so directed, error and leaves the
 * lock held otherwise.
 */
int do_vmi_munmap(struct vma_iterator *vmi, struct mm_struct *mm,
		  unsigned long start, size_t len, struct list_head *uf,
		  bool unlock)
{
	unsigned long end;
	struct vm_area_struct *vma;

	if ((offset_in_page(start)) || start > TASK_SIZE || len > TASK_SIZE-start)
		return -EINVAL;

	end = start + PAGE_ALIGN(len);
	if (end == start)
		return -EINVAL;

	/* Find the first overlapping VMA */
	vma = vma_find(vmi, end);
	if (!vma) {
		if (unlock)
			mmap_write_unlock(mm);
		return 0;
	}

	return do_vmi_align_munmap(vmi, vma, mm, start, end, uf, unlock);
}

/*
 * Given a mapping request (addr,end,vm_flags,file,pgoff,anon_name),
 * figure out whether that can be merged with its predecessor or its
 * successor.  Or both (it neatly fills a hole).
 *
 * In most cases - when called for mmap, brk or mremap - [addr,end) is
 * certain not to be mapped by the time vma_merge is called; but when
 * called for mprotect, it is certain to be already mapped (either at
 * an offset within prev, or at the start of next), and the flags of
 * this area are about to be changed to vm_flags - and the no-change
 * case has already been eliminated.
 *
 * The following mprotect cases have to be considered, where **** is
 * the area passed down from mprotect_fixup, never extending beyond one
 * vma, PPPP is the previous vma, CCCC is a concurrent vma that starts
 * at the same address as **** and is of the same or larger span, and
 * NNNN the next vma after ****:
 *
 *     ****             ****                   ****
 *    PPPPPPNNNNNN    PPPPPPNNNNNN       PPPPPPCCCCCC
 *    cannot merge    might become       might become
 *                    PPNNNNNNNNNN       PPPPPPPPPPCC
 *    mmap, brk or    case 4 below       case 5 below
 *    mremap move:
 *                        ****               ****
 *                    PPPP    NNNN       PPPPCCCCNNNN
 *                    might become       might become
 *                    PPPPPPPPPPPP 1 or  PPPPPPPPPPPP 6 or
 *                    PPPPPPPPNNNN 2 or  PPPPPPPPNNNN 7 or
 *                    PPPPNNNNNNNN 3     PPPPNNNNNNNN 8
 *
 * It is important for case 8 that the vma CCCC overlapping the
 * region **** is never going to extended over NNNN. Instead NNNN must
 * be extended in region **** and CCCC must be removed. This way in
 * all cases where vma_merge succeeds, the moment vma_merge drops the
 * rmap_locks, the properties of the merged vma will be already
 * correct for the whole merged range. Some of those properties like
 * vm_page_prot/vm_flags may be accessed by rmap_walks and they must
 * be correct for the whole merged range immediately after the
 * rmap_locks are released. Otherwise if NNNN would be removed and
 * CCCC would be extended over the NNNN range, remove_migration_ptes
 * or other rmap walkers (if working on addresses beyond the "end"
 * parameter) may establish ptes with the wrong permissions of CCCC
 * instead of the right permissions of NNNN.
 *
 * In the code below:
 * PPPP is represented by *prev
 * CCCC is represented by *curr or not represented at all (NULL)
 * NNNN is represented by *next or not represented at all (NULL)
 * **** is not represented - it will be merged and the vma containing the
 *      area is returned, or the function will return NULL
 */
static struct vm_area_struct
*vma_merge(struct vma_iterator *vmi, struct vm_area_struct *prev,
	   struct vm_area_struct *src, unsigned long addr, unsigned long end,
	   unsigned long vm_flags, pgoff_t pgoff, struct mempolicy *policy,
	   struct vm_userfaultfd_ctx vm_userfaultfd_ctx,
	   struct anon_vma_name *anon_name)
{
	struct mm_struct *mm = src->vm_mm;
	struct anon_vma *anon_vma = src->anon_vma;
	struct file *file = src->vm_file;
	struct vm_area_struct *curr, *next, *res;
	struct vm_area_struct *vma, *adjust, *remove, *remove2;
	struct vm_area_struct *anon_dup = NULL;
	struct vma_prepare vp;
	pgoff_t vma_pgoff;
	int err = 0;
	bool merge_prev = false;
	bool merge_next = false;
	bool vma_expanded = false;
	unsigned long vma_start = addr;
	unsigned long vma_end = end;
	pgoff_t pglen = (end - addr) >> PAGE_SHIFT;
	long adj_start = 0;

	/*
	 * We later require that vma->vm_flags == vm_flags,
	 * so this tests vma->vm_flags & VM_SPECIAL, too.
	 */
	if (vm_flags & VM_SPECIAL)
		return NULL;

	/* Does the input range span an existing VMA? (cases 5 - 8) */
	curr = find_vma_intersection(mm, prev ? prev->vm_end : 0, end);

	if (!curr ||			/* cases 1 - 4 */
	    end == curr->vm_end)	/* cases 6 - 8, adjacent VMA */
		next = vma_lookup(mm, end);
	else
		next = NULL;		/* case 5 */

	if (prev) {
		vma_start = prev->vm_start;
		vma_pgoff = prev->vm_pgoff;

		/* Can we merge the predecessor? */
		if (addr == prev->vm_end && mpol_equal(vma_policy(prev), policy)
		    && can_vma_merge_after(prev, vm_flags, anon_vma, file,
					   pgoff, vm_userfaultfd_ctx, anon_name)) {
			merge_prev = true;
			vma_prev(vmi);
		}
	}

	/* Can we merge the successor? */
	if (next && mpol_equal(policy, vma_policy(next)) &&
	    can_vma_merge_before(next, vm_flags, anon_vma, file, pgoff+pglen,
				 vm_userfaultfd_ctx, anon_name)) {
		merge_next = true;
	}

	/* Verify some invariant that must be enforced by the caller. */
	VM_WARN_ON(prev && addr <= prev->vm_start);
	VM_WARN_ON(curr && (addr != curr->vm_start || end > curr->vm_end));
	VM_WARN_ON(addr >= end);

	if (!merge_prev && !merge_next)
		return NULL; /* Not mergeable. */

	if (merge_prev)
		vma_start_write(prev);

	res = vma = prev;
	remove = remove2 = adjust = NULL;

	/* Can we merge both the predecessor and the successor? */
	if (merge_prev && merge_next &&
	    is_mergeable_anon_vma(prev->anon_vma, next->anon_vma, NULL)) {
		vma_start_write(next);
		remove = next;				/* case 1 */
		vma_end = next->vm_end;
		err = dup_anon_vma(prev, next, &anon_dup);
		if (curr) {				/* case 6 */
			vma_start_write(curr);
			remove = curr;
			remove2 = next;
			/*
			 * Note that the dup_anon_vma below cannot overwrite err
			 * since the first caller would do nothing unless next
			 * has an anon_vma.
			 */
			if (!next->anon_vma)
				err = dup_anon_vma(prev, curr, &anon_dup);
		}
	} else if (merge_prev) {			/* case 2 */
		if (curr) {
			vma_start_write(curr);
			if (end == curr->vm_end) {	/* case 7 */
				/*
				 * can_vma_merge_after() assumed we would not be
				 * removing prev vma, so it skipped the check
				 * for vm_ops->close, but we are removing curr
				 */
				if (curr->vm_ops && curr->vm_ops->close)
					err = -EINVAL;
				remove = curr;
			} else {			/* case 5 */
				adjust = curr;
				adj_start = (end - curr->vm_start);
			}
			if (!err)
				err = dup_anon_vma(prev, curr, &anon_dup);
		}
	} else { /* merge_next */
		vma_start_write(next);
		res = next;
		if (prev && addr < prev->vm_end) {	/* case 4 */
			vma_start_write(prev);
			vma_end = addr;
			adjust = next;
			adj_start = -(prev->vm_end - addr);
			err = dup_anon_vma(next, prev, &anon_dup);
		} else {
			/*
			 * Note that cases 3 and 8 are the ONLY ones where prev
			 * is permitted to be (but is not necessarily) NULL.
			 */
			vma = next;			/* case 3 */
			vma_start = addr;
			vma_end = next->vm_end;
			vma_pgoff = next->vm_pgoff - pglen;
			if (curr) {			/* case 8 */
				vma_pgoff = curr->vm_pgoff;
				vma_start_write(curr);
				remove = curr;
				err = dup_anon_vma(next, curr, &anon_dup);
			}
		}
	}

	/* Error in anon_vma clone. */
	if (err)
		goto anon_vma_fail;

	if (vma_start < vma->vm_start || vma_end > vma->vm_end)
		vma_expanded = true;

	if (vma_expanded) {
		vma_iter_config(vmi, vma_start, vma_end);
	} else {
		vma_iter_config(vmi, adjust->vm_start + adj_start,
				adjust->vm_end);
	}

	if (vma_iter_prealloc(vmi, vma))
		goto prealloc_fail;

	init_multi_vma_prep(&vp, vma, adjust, remove, remove2);
	VM_WARN_ON(vp.anon_vma && adjust && adjust->anon_vma &&
		   vp.anon_vma != adjust->anon_vma);

	vma_prepare(&vp);
	vma_adjust_trans_huge(vma, vma_start, vma_end, adj_start);
	vma_set_range(vma, vma_start, vma_end, vma_pgoff);

	if (vma_expanded)
		vma_iter_store(vmi, vma);

	if (adj_start) {
		adjust->vm_start += adj_start;
		adjust->vm_pgoff += adj_start >> PAGE_SHIFT;
		if (adj_start < 0) {
			WARN_ON(vma_expanded);
			vma_iter_store(vmi, next);
		}
	}

	vma_complete(&vp, vmi, mm);
	khugepaged_enter_vma(res, vm_flags);
	return res;

prealloc_fail:
	if (anon_dup)
		unlink_anon_vmas(anon_dup);

anon_vma_fail:
	vma_iter_set(vmi, addr);
	vma_iter_load(vmi);
	return NULL;
}

/*
 * We are about to modify one or multiple of a VMA's flags, policy, userfaultfd
 * context and anonymous VMA name within the range [start, end).
 *
 * As a result, we might be able to merge the newly modified VMA range with an
 * adjacent VMA with identical properties.
 *
 * If no merge is possible and the range does not span the entirety of the VMA,
 * we then need to split the VMA to accommodate the change.
 *
 * The function returns either the merged VMA, the original VMA if a split was
 * required instead, or an error if the split failed.
 */
struct vm_area_struct *vma_modify(struct vma_iterator *vmi,
				  struct vm_area_struct *prev,
				  struct vm_area_struct *vma,
				  unsigned long start, unsigned long end,
				  unsigned long vm_flags,
				  struct mempolicy *policy,
				  struct vm_userfaultfd_ctx uffd_ctx,
				  struct anon_vma_name *anon_name)
{
	pgoff_t pgoff = vma->vm_pgoff + ((start - vma->vm_start) >> PAGE_SHIFT);
	struct vm_area_struct *merged;

	merged = vma_merge(vmi, prev, vma, start, end, vm_flags,
			   pgoff, policy, uffd_ctx, anon_name);
	if (merged)
		return merged;

	if (vma->vm_start < start) {
		int err = split_vma(vmi, vma, start, 1);

		if (err)
			return ERR_PTR(err);
	}

	if (vma->vm_end > end) {
		int err = split_vma(vmi, vma, end, 0);

		if (err)
			return ERR_PTR(err);
	}

	return vma;
}

/*
 * Attempt to merge a newly mapped VMA with those adjacent to it. The caller
 * must ensure that [start, end) does not overlap any existing VMA.
 */
struct vm_area_struct
*vma_merge_new_vma(struct vma_iterator *vmi, struct vm_area_struct *prev,
		   struct vm_area_struct *vma, unsigned long start,
		   unsigned long end, pgoff_t pgoff)
{
	return vma_merge(vmi, prev, vma, start, end, vma->vm_flags, pgoff,
			 vma_policy(vma), vma->vm_userfaultfd_ctx, anon_vma_name(vma));
}

/*
 * Expand vma by delta bytes, potentially merging with an immediately adjacent
 * VMA with identical properties.
 */
struct vm_area_struct *vma_merge_extend(struct vma_iterator *vmi,
					struct vm_area_struct *vma,
					unsigned long delta)
{
	pgoff_t pgoff = vma->vm_pgoff + vma_pages(vma);

	/* vma is specified as prev, so case 1 or 2 will apply. */
	return vma_merge(vmi, vma, vma, vma->vm_end, vma->vm_end + delta,
			 vma->vm_flags, pgoff, vma_policy(vma),
			 vma->vm_userfaultfd_ctx, anon_vma_name(vma));
}

void unlink_file_vma_batch_init(struct unlink_vma_file_batch *vb)
{
	vb->count = 0;
}

static void unlink_file_vma_batch_process(struct unlink_vma_file_batch *vb)
{
	struct address_space *mapping;
	int i;

	mapping = vb->vmas[0]->vm_file->f_mapping;
	i_mmap_lock_write(mapping);
	for (i = 0; i < vb->count; i++) {
		VM_WARN_ON_ONCE(vb->vmas[i]->vm_file->f_mapping != mapping);
		__remove_shared_vm_struct(vb->vmas[i], mapping);
	}
	i_mmap_unlock_write(mapping);

	unlink_file_vma_batch_init(vb);
}

void unlink_file_vma_batch_add(struct unlink_vma_file_batch *vb,
			       struct vm_area_struct *vma)
{
	if (vma->vm_file == NULL)
		return;

	if ((vb->count > 0 && vb->vmas[0]->vm_file != vma->vm_file) ||
	    vb->count == ARRAY_SIZE(vb->vmas))
		unlink_file_vma_batch_process(vb);

	vb->vmas[vb->count] = vma;
	vb->count++;
}

void unlink_file_vma_batch_final(struct unlink_vma_file_batch *vb)
{
	if (vb->count > 0)
		unlink_file_vma_batch_process(vb);
}

/*
 * Unlink a file-based vm structure from its interval tree, to hide
 * vma from rmap and vmtruncate before freeing its page tables.
 */
void unlink_file_vma(struct vm_area_struct *vma)
{
	struct file *file = vma->vm_file;

	if (file) {
		struct address_space *mapping = file->f_mapping;

		i_mmap_lock_write(mapping);
		__remove_shared_vm_struct(vma, mapping);
		i_mmap_unlock_write(mapping);
	}
}

void vma_link_file(struct vm_area_struct *vma)
{
	struct file *file = vma->vm_file;
	struct address_space *mapping;

	if (file) {
		mapping = file->f_mapping;
		i_mmap_lock_write(mapping);
		__vma_link_file(vma, mapping);
		i_mmap_unlock_write(mapping);
	}
}

int vma_link(struct mm_struct *mm, struct vm_area_struct *vma)
{
	VMA_ITERATOR(vmi, mm, 0);

	vma_iter_config(&vmi, vma->vm_start, vma->vm_end);
	if (vma_iter_prealloc(&vmi, vma))
		return -ENOMEM;

	vma_start_write(vma);
	vma_iter_store(&vmi, vma);
	vma_link_file(vma);
	mm->map_count++;
	validate_mm(mm);
	return 0;
}

/*
 * Copy the vma structure to a new location in the same mm,
 * prior to moving page table entries, to effect an mremap move.
 */
struct vm_area_struct *copy_vma(struct vm_area_struct **vmap,
	unsigned long addr, unsigned long len, pgoff_t pgoff,
	bool *need_rmap_locks)
{
	struct vm_area_struct *vma = *vmap;
	unsigned long vma_start = vma->vm_start;
	struct mm_struct *mm = vma->vm_mm;
	struct vm_area_struct *new_vma, *prev;
	bool faulted_in_anon_vma = true;
	VMA_ITERATOR(vmi, mm, addr);

	/*
	 * If anonymous vma has not yet been faulted, update new pgoff
	 * to match new location, to increase its chance of merging.
	 */
	if (unlikely(vma_is_anonymous(vma) && !vma->anon_vma)) {
		pgoff = addr >> PAGE_SHIFT;
		faulted_in_anon_vma = false;
	}

	new_vma = find_vma_prev(mm, addr, &prev);
	if (new_vma && new_vma->vm_start < addr + len)
		return NULL;	/* should never get here */

	new_vma = vma_merge_new_vma(&vmi, prev, vma, addr, addr + len, pgoff);
	if (new_vma) {
		/*
		 * Source vma may have been merged into new_vma
		 */
		if (unlikely(vma_start >= new_vma->vm_start &&
			     vma_start < new_vma->vm_end)) {
			/*
			 * The only way we can get a vma_merge with
			 * self during an mremap is if the vma hasn't
			 * been faulted in yet and we were allowed to
			 * reset the dst vma->vm_pgoff to the
			 * destination address of the mremap to allow
			 * the merge to happen. mremap must change the
			 * vm_pgoff linearity between src and dst vmas
			 * (in turn preventing a vma_merge) to be
			 * safe. It is only safe to keep the vm_pgoff
			 * linear if there are no pages mapped yet.
			 */
			VM_BUG_ON_VMA(faulted_in_anon_vma, new_vma);
			*vmap = vma = new_vma;
		}
		*need_rmap_locks = (new_vma->vm_pgoff <= vma->vm_pgoff);
	} else {
		new_vma = vm_area_dup(vma);
		if (!new_vma)
			goto out;
		vma_set_range(new_vma, addr, addr + len, pgoff);
		if (vma_dup_policy(vma, new_vma))
			goto out_free_vma;
		if (anon_vma_clone(new_vma, vma))
			goto out_free_mempol;
		if (new_vma->vm_file)
			get_file(new_vma->vm_file);
		if (new_vma->vm_ops && new_vma->vm_ops->open)
			new_vma->vm_ops->open(new_vma);
		if (vma_link(mm, new_vma))
			goto out_vma_link;
		*need_rmap_locks = false;
	}
	return new_vma;

out_vma_link:
	if (new_vma->vm_ops && new_vma->vm_ops->close)
		new_vma->vm_ops->close(new_vma);

	if (new_vma->vm_file)
		fput(new_vma->vm_file);

	unlink_anon_vmas(new_vma);
out_free_mempol:
	mpol_put(vma_policy(new_vma));
out_free_vma:
	vm_area_free(new_vma);
out:
	return NULL;
}

/*
 * Rough compatibility check to quickly see if it's even worth looking
 * at sharing an anon_vma.
 *
 * They need to have the same vm_file, and the flags can only differ
 * in things that mprotect may change.
 *
 * NOTE! The fact that we share an anon_vma doesn't _have_ to mean that
 * we can merge the two vma's. For example, we refuse to merge a vma if
 * there is a vm_ops->close() function, because that indicates that the
 * driver is doing some kind of reference counting. But that doesn't
 * really matter for the anon_vma sharing case.
 */
static int anon_vma_compatible(struct vm_area_struct *a, struct vm_area_struct *b)
{
	return a->vm_end == b->vm_start &&
		mpol_equal(vma_policy(a), vma_policy(b)) &&
		a->vm_file == b->vm_file &&
		!((a->vm_flags ^ b->vm_flags) & ~(VM_ACCESS_FLAGS | VM_SOFTDIRTY)) &&
		b->vm_pgoff == a->vm_pgoff + ((b->vm_start - a->vm_start) >> PAGE_SHIFT);
}

/*
 * Do some basic sanity checking to see if we can re-use the anon_vma
 * from 'old'. The 'a'/'b' vma's are in VM order - one of them will be
 * the same as 'old', the other will be the new one that is trying
 * to share the anon_vma.
 *
 * NOTE! This runs with mmap_lock held for reading, so it is possible that
 * the anon_vma of 'old' is concurrently in the process of being set up
 * by another page fault trying to merge _that_. But that's ok: if it
 * is being set up, that automatically means that it will be a singleton
 * acceptable for merging, so we can do all of this optimistically. But
 * we do that READ_ONCE() to make sure that we never re-load the pointer.
 *
 * IOW: that the "list_is_singular()" test on the anon_vma_chain only
 * matters for the 'stable anon_vma' case (ie the thing we want to avoid
 * is to return an anon_vma that is "complex" due to having gone through
 * a fork).
 *
 * We also make sure that the two vma's are compatible (adjacent,
 * and with the same memory policies). That's all stable, even with just
 * a read lock on the mmap_lock.
 */
static struct anon_vma *reusable_anon_vma(struct vm_area_struct *old,
					  struct vm_area_struct *a,
					  struct vm_area_struct *b)
{
	if (anon_vma_compatible(a, b)) {
		struct anon_vma *anon_vma = READ_ONCE(old->anon_vma);

		if (anon_vma && list_is_singular(&old->anon_vma_chain))
			return anon_vma;
	}
	return NULL;
}

/*
 * find_mergeable_anon_vma is used by anon_vma_prepare, to check
 * neighbouring vmas for a suitable anon_vma, before it goes off
 * to allocate a new anon_vma.  It checks because a repetitive
 * sequence of mprotects and faults may otherwise lead to distinct
 * anon_vmas being allocated, preventing vma merge in subsequent
 * mprotect.
 */
struct anon_vma *find_mergeable_anon_vma(struct vm_area_struct *vma)
{
	struct anon_vma *anon_vma = NULL;
	struct vm_area_struct *prev, *next;
	VMA_ITERATOR(vmi, vma->vm_mm, vma->vm_end);

	/* Try next first. */
	next = vma_iter_load(&vmi);
	if (next) {
		anon_vma = reusable_anon_vma(next, vma, next);
		if (anon_vma)
			return anon_vma;
	}

	prev = vma_prev(&vmi);
	VM_BUG_ON_VMA(prev != vma, vma);
	prev = vma_prev(&vmi);
	/* Try prev next. */
	if (prev)
		anon_vma = reusable_anon_vma(prev, prev, vma);

	/*
	 * We might reach here with anon_vma == NULL if we can't find
	 * any reusable anon_vma.
	 * There's no absolute need to look only at touching neighbours:
	 * we could search further afield for "compatible" anon_vmas.
	 * But it would probably just be a waste of time searching,
	 * or lead to too many vmas hanging off the same anon_vma.
	 * We're trying to allow mprotect remerging later on,
	 * not trying to minimize memory used for anon_vmas.
	 */
	return anon_vma;
}

static bool vm_ops_needs_writenotify(const struct vm_operations_struct *vm_ops)
{
	return vm_ops && (vm_ops->page_mkwrite || vm_ops->pfn_mkwrite);
}

static bool vma_is_shared_writable(struct vm_area_struct *vma)
{
	return (vma->vm_flags & (VM_WRITE | VM_SHARED)) ==
		(VM_WRITE | VM_SHARED);
}

static bool vma_fs_can_writeback(struct vm_area_struct *vma)
{
	/* No managed pages to writeback. */
	if (vma->vm_flags & VM_PFNMAP)
		return false;

	return vma->vm_file && vma->vm_file->f_mapping &&
		mapping_can_writeback(vma->vm_file->f_mapping);
}

/*
 * Does this VMA require the underlying folios to have their dirty state
 * tracked?
 */
bool vma_needs_dirty_tracking(struct vm_area_struct *vma)
{
	/* Only shared, writable VMAs require dirty tracking. */
	if (!vma_is_shared_writable(vma))
		return false;

	/* Does the filesystem need to be notified? */
	if (vm_ops_needs_writenotify(vma->vm_ops))
		return true;

	/*
	 * Even if the filesystem doesn't indicate a need for writenotify, if it
	 * can writeback, dirty tracking is still required.
	 */
	return vma_fs_can_writeback(vma);
}

/*
 * Some shared mappings will want the pages marked read-only
 * to track write events. If so, we'll downgrade vm_page_prot
 * to the private version (using protection_map[] without the
 * VM_SHARED bit).
 */
bool vma_wants_writenotify(struct vm_area_struct *vma, pgprot_t vm_page_prot)
{
	/* If it was private or non-writable, the write bit is already clear */
	if (!vma_is_shared_writable(vma))
		return false;

	/* The backer wishes to know when pages are first written to? */
	if (vm_ops_needs_writenotify(vma->vm_ops))
		return true;

	/* The open routine did something to the protections that pgprot_modify
	 * won't preserve? */
	if (pgprot_val(vm_page_prot) !=
	    pgprot_val(vm_pgprot_modify(vm_page_prot, vma->vm_flags)))
		return false;

	/*
	 * Do we need to track softdirty? hugetlb does not support softdirty
	 * tracking yet.
	 */
	if (vma_soft_dirty_enabled(vma) && !is_vm_hugetlb_page(vma))
		return true;

	/* Do we need write faults for uffd-wp tracking? */
	if (userfaultfd_wp(vma))
		return true;

	/* Can the mapping track the dirty pages? */
	return vma_fs_can_writeback(vma);
}

unsigned long count_vma_pages_range(struct mm_struct *mm,
				    unsigned long addr, unsigned long end)
{
	VMA_ITERATOR(vmi, mm, addr);
	struct vm_area_struct *vma;
	unsigned long nr_pages = 0;

	for_each_vma_range(vmi, vma, end) {
		unsigned long vm_start = max(addr, vma->vm_start);
		unsigned long vm_end = min(end, vma->vm_end);

		nr_pages += PHYS_PFN(vm_end - vm_start);
	}

	return nr_pages;
}

static DEFINE_MUTEX(mm_all_locks_mutex);

static void vm_lock_anon_vma(struct mm_struct *mm, struct anon_vma *anon_vma)
{
	if (!test_bit(0, (unsigned long *) &anon_vma->root->rb_root.rb_root.rb_node)) {
		/*
		 * The LSB of head.next can't change from under us
		 * because we hold the mm_all_locks_mutex.
		 */
		down_write_nest_lock(&anon_vma->root->rwsem, &mm->mmap_lock);
		/*
		 * We can safely modify head.next after taking the
		 * anon_vma->root->rwsem. If some other vma in this mm shares
		 * the same anon_vma we won't take it again.
		 *
		 * No need of atomic instructions here, head.next
		 * can't change from under us thanks to the
		 * anon_vma->root->rwsem.
		 */
		if (__test_and_set_bit(0, (unsigned long *)
				       &anon_vma->root->rb_root.rb_root.rb_node))
			BUG();
	}
}

static void vm_lock_mapping(struct mm_struct *mm, struct address_space *mapping)
{
	if (!test_bit(AS_MM_ALL_LOCKS, &mapping->flags)) {
		/*
		 * AS_MM_ALL_LOCKS can't change from under us because
		 * we hold the mm_all_locks_mutex.
		 *
		 * Operations on ->flags have to be atomic because
		 * even if AS_MM_ALL_LOCKS is stable thanks to the
		 * mm_all_locks_mutex, there may be other cpus
		 * changing other bitflags in parallel to us.
		 */
		if (test_and_set_bit(AS_MM_ALL_LOCKS, &mapping->flags))
			BUG();
		down_write_nest_lock(&mapping->i_mmap_rwsem, &mm->mmap_lock);
	}
}

/*
 * This operation locks against the VM for all pte/vma/mm related
 * operations that could ever happen on a certain mm. This includes
 * vmtruncate, try_to_unmap, and all page faults.
 *
 * The caller must take the mmap_lock in write mode before calling
 * mm_take_all_locks(). The caller isn't allowed to release the
 * mmap_lock until mm_drop_all_locks() returns.
 *
 * mmap_lock in write mode is required in order to block all operations
 * that could modify pagetables and free pages without need of
 * altering the vma layout. It's also needed in write mode to avoid new
 * anon_vmas to be associated with existing vmas.
 *
 * A single task can't take more than one mm_take_all_locks() in a row
 * or it would deadlock.
 *
 * The LSB in anon_vma->rb_root.rb_node and the AS_MM_ALL_LOCKS bitflag in
 * mapping->flags avoid to take the same lock twice, if more than one
 * vma in this mm is backed by the same anon_vma or address_space.
 *
 * We take locks in following order, accordingly to comment at beginning
 * of mm/rmap.c:
 *   - all hugetlbfs_i_mmap_rwsem_key locks (aka mapping->i_mmap_rwsem for
 *     hugetlb mapping);
 *   - all vmas marked locked
 *   - all i_mmap_rwsem locks;
 *   - all anon_vma->rwseml
 *
 * We can take all locks within these types randomly because the VM code
 * doesn't nest them and we protected from parallel mm_take_all_locks() by
 * mm_all_locks_mutex.
 *
 * mm_take_all_locks() and mm_drop_all_locks are expensive operations
 * that may have to take thousand of locks.
 *
 * mm_take_all_locks() can fail if it's interrupted by signals.
 */
int mm_take_all_locks(struct mm_struct *mm)
{
	struct vm_area_struct *vma;
	struct anon_vma_chain *avc;
	VMA_ITERATOR(vmi, mm, 0);

	mmap_assert_write_locked(mm);

	mutex_lock(&mm_all_locks_mutex);

	/*
	 * vma_start_write() does not have a complement in mm_drop_all_locks()
	 * because vma_start_write() is always asymmetrical; it marks a VMA as
	 * being written to until mmap_write_unlock() or mmap_write_downgrade()
	 * is reached.
	 */
	for_each_vma(vmi, vma) {
		if (signal_pending(current))
			goto out_unlock;
		vma_start_write(vma);
	}

	vma_iter_init(&vmi, mm, 0);
	for_each_vma(vmi, vma) {
		if (signal_pending(current))
			goto out_unlock;
		if (vma->vm_file && vma->vm_file->f_mapping &&
				is_vm_hugetlb_page(vma))
			vm_lock_mapping(mm, vma->vm_file->f_mapping);
	}

	vma_iter_init(&vmi, mm, 0);
	for_each_vma(vmi, vma) {
		if (signal_pending(current))
			goto out_unlock;
		if (vma->vm_file && vma->vm_file->f_mapping &&
				!is_vm_hugetlb_page(vma))
			vm_lock_mapping(mm, vma->vm_file->f_mapping);
	}

	vma_iter_init(&vmi, mm, 0);
	for_each_vma(vmi, vma) {
		if (signal_pending(current))
			goto out_unlock;
		if (vma->anon_vma)
			list_for_each_entry(avc, &vma->anon_vma_chain, same_vma)
				vm_lock_anon_vma(mm, avc->anon_vma);
	}

	return 0;

out_unlock:
	mm_drop_all_locks(mm);
	return -EINTR;
}

static void vm_unlock_anon_vma(struct anon_vma *anon_vma)
{
	if (test_bit(0, (unsigned long *) &anon_vma->root->rb_root.rb_root.rb_node)) {
		/*
		 * The LSB of head.next can't change to 0 from under
		 * us because we hold the mm_all_locks_mutex.
		 *
		 * We must however clear the bitflag before unlocking
		 * the vma so the users using the anon_vma->rb_root will
		 * never see our bitflag.
		 *
		 * No need of atomic instructions here, head.next
		 * can't change from under us until we release the
		 * anon_vma->root->rwsem.
		 */
		if (!__test_and_clear_bit(0, (unsigned long *)
					  &anon_vma->root->rb_root.rb_root.rb_node))
			BUG();
		anon_vma_unlock_write(anon_vma);
	}
}

static void vm_unlock_mapping(struct address_space *mapping)
{
	if (test_bit(AS_MM_ALL_LOCKS, &mapping->flags)) {
		/*
		 * AS_MM_ALL_LOCKS can't change to 0 from under us
		 * because we hold the mm_all_locks_mutex.
		 */
		i_mmap_unlock_write(mapping);
		if (!test_and_clear_bit(AS_MM_ALL_LOCKS,
					&mapping->flags))
			BUG();
	}
}

/*
 * The mmap_lock cannot be released by the caller until
 * mm_drop_all_locks() returns.
 */
void mm_drop_all_locks(struct mm_struct *mm)
{
	struct vm_area_struct *vma;
	struct anon_vma_chain *avc;
	VMA_ITERATOR(vmi, mm, 0);

	mmap_assert_write_locked(mm);
	BUG_ON(!mutex_is_locked(&mm_all_locks_mutex));

	for_each_vma(vmi, vma) {
		if (vma->anon_vma)
			list_for_each_entry(avc, &vma->anon_vma_chain, same_vma)
				vm_unlock_anon_vma(avc->anon_vma);
		if (vma->vm_file && vma->vm_file->f_mapping)
			vm_unlock_mapping(vma->vm_file->f_mapping);
	}

	mutex_unlock(&mm_all_locks_mutex);
}
