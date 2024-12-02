// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * VMA-specific functions.
 */

#include "vma_internal.h"
#include "vma.h"

struct mmap_state {
	struct mm_struct *mm;
	struct vma_iterator *vmi;

	unsigned long addr;
	unsigned long end;
	pgoff_t pgoff;
	unsigned long pglen;
	unsigned long flags;
	struct file *file;

	unsigned long charged;
	bool retry_merge;

	struct vm_area_struct *prev;
	struct vm_area_struct *next;

	/* Unmapping state. */
	struct vma_munmap_struct vms;
	struct ma_state mas_detach;
	struct maple_tree mt_detach;
};

#define MMAP_STATE(name, mm_, vmi_, addr_, len_, pgoff_, flags_, file_) \
	struct mmap_state name = {					\
		.mm = mm_,						\
		.vmi = vmi_,						\
		.addr = addr_,						\
		.end = (addr_) + len,					\
		.pgoff = pgoff_,					\
		.pglen = PHYS_PFN(len_),				\
		.flags = flags_,					\
		.file = file_,						\
	}

#define VMG_MMAP_STATE(name, map_, vma_)				\
	struct vma_merge_struct name = {				\
		.mm = (map_)->mm,					\
		.vmi = (map_)->vmi,					\
		.start = (map_)->addr,					\
		.end = (map_)->end,					\
		.flags = (map_)->flags,					\
		.pgoff = (map_)->pgoff,					\
		.file = (map_)->file,					\
		.prev = (map_)->prev,					\
		.vma = vma_,						\
		.next = (vma_) ? NULL : (map_)->next,			\
		.state = VMA_MERGE_START,				\
		.merge_flags = VMG_FLAG_DEFAULT,			\
	}

static inline bool is_mergeable_vma(struct vma_merge_struct *vmg, bool merge_next)
{
	struct vm_area_struct *vma = merge_next ? vmg->next : vmg->prev;

	if (!mpol_equal(vmg->policy, vma_policy(vma)))
		return false;
	/*
	 * VM_SOFTDIRTY should not prevent from VMA merging, if we
	 * match the flags but dirty bit -- the caller should mark
	 * merged VMA as dirty. If dirty bit won't be excluded from
	 * comparison, we increase pressure on the memory system forcing
	 * the kernel to generate new VMAs when old one could be
	 * extended instead.
	 */
	if ((vma->vm_flags ^ vmg->flags) & ~VM_SOFTDIRTY)
		return false;
	if (vma->vm_file != vmg->file)
		return false;
	if (!is_mergeable_vm_userfaultfd_ctx(vma, vmg->uffd_ctx))
		return false;
	if (!anon_vma_name_eq(anon_vma_name(vma), vmg->anon_name))
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

/* Are the anon_vma's belonging to each VMA compatible with one another? */
static inline bool are_anon_vmas_compatible(struct vm_area_struct *vma1,
					    struct vm_area_struct *vma2)
{
	return is_mergeable_anon_vma(vma1->anon_vma, vma2->anon_vma, NULL);
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
static bool can_vma_merge_before(struct vma_merge_struct *vmg)
{
	pgoff_t pglen = PHYS_PFN(vmg->end - vmg->start);

	if (is_mergeable_vma(vmg, /* merge_next = */ true) &&
	    is_mergeable_anon_vma(vmg->anon_vma, vmg->next->anon_vma, vmg->next)) {
		if (vmg->next->vm_pgoff == vmg->pgoff + pglen)
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
static bool can_vma_merge_after(struct vma_merge_struct *vmg)
{
	if (is_mergeable_vma(vmg, /* merge_next = */ false) &&
	    is_mergeable_anon_vma(vmg->anon_vma, vmg->prev->anon_vma, vmg->prev)) {
		if (vmg->prev->vm_pgoff + vma_pages(vmg->prev) == vmg->pgoff)
			return true;
	}
	return false;
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
 * vma_prepare() - Helper function for handling locking VMAs prior to altering
 * @vp: The initialized vma_prepare struct
 */
static void vma_prepare(struct vma_prepare *vp)
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
 * vma_complete- Helper function for handling the unlocking after altering VMAs,
 * or for inserting a VMA.
 *
 * @vp: The vma_prepare struct
 * @vmi: The vma iterator
 * @mm: The mm_struct
 */
static void vma_complete(struct vma_prepare *vp, struct vma_iterator *vmi,
			 struct mm_struct *mm)
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
}

/*
 * init_vma_prep() - Initializer wrapper for vma_prepare struct
 * @vp: The vma_prepare struct
 * @vma: The vma that will be altered once locked
 */
static void init_vma_prep(struct vma_prepare *vp, struct vm_area_struct *vma)
{
	init_multi_vma_prep(vp, vma, NULL, NULL, NULL);
}

/*
 * Can the proposed VMA be merged with the left (previous) VMA taking into
 * account the start position of the proposed range.
 */
static bool can_vma_merge_left(struct vma_merge_struct *vmg)

{
	return vmg->prev && vmg->prev->vm_end == vmg->start &&
		can_vma_merge_after(vmg);
}

/*
 * Can the proposed VMA be merged with the right (next) VMA taking into
 * account the end position of the proposed range.
 *
 * In addition, if we can merge with the left VMA, ensure that left and right
 * anon_vma's are also compatible.
 */
static bool can_vma_merge_right(struct vma_merge_struct *vmg,
				bool can_merge_left)
{
	if (!vmg->next || vmg->end != vmg->next->vm_start ||
	    !can_vma_merge_before(vmg))
		return false;

	if (!can_merge_left)
		return true;

	/*
	 * If we can merge with prev (left) and next (right), indicating that
	 * each VMA's anon_vma is compatible with the proposed anon_vma, this
	 * does not mean prev and next are compatible with EACH OTHER.
	 *
	 * We therefore check this in addition to mergeability to either side.
	 */
	return are_anon_vmas_compatible(vmg->prev, vmg->next);
}

/*
 * Close a vm structure and free it.
 */
void remove_vma(struct vm_area_struct *vma, bool unreachable)
{
	might_sleep();
	vma_close(vma);
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
void unmap_region(struct ma_state *mas, struct vm_area_struct *vma,
		struct vm_area_struct *prev, struct vm_area_struct *next)
{
	struct mm_struct *mm = vma->vm_mm;
	struct mmu_gather tlb;

	lru_add_drain();
	tlb_gather_mmu(&tlb, mm);
	update_hiwater_rss(mm);
	unmap_vmas(&tlb, mas, vma, vma->vm_start, vma->vm_end, vma->vm_end,
		   /* mm_wr_locked = */ true);
	mas_set(mas, vma->vm_end);
	free_pgtables(&tlb, mas, vma, prev ? prev->vm_end : FIRST_USER_ADDRESS,
		      next ? next->vm_start : USER_PGTABLES_CEILING,
		      /* mm_wr_locked = */ true);
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
	validate_mm(vma->vm_mm);

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
		/* Check for a infinite loop */
		if (++i > mm->map_count + 10) {
			i = -1;
			break;
		}
	}
	if (i != mm->map_count) {
		pr_emerg("map_count %d vma iterator %d\n", mm->map_count, i);
		bug = 1;
	}
	VM_BUG_ON_MM(bug, mm);
}
#endif /* CONFIG_DEBUG_VM_MAPLE_TREE */

/* Actually perform the VMA merge operation. */
static int commit_merge(struct vma_merge_struct *vmg,
			struct vm_area_struct *adjust,
			struct vm_area_struct *remove,
			struct vm_area_struct *remove2,
			long adj_start,
			bool expanded)
{
	struct vma_prepare vp;

	init_multi_vma_prep(&vp, vmg->vma, adjust, remove, remove2);

	VM_WARN_ON(vp.anon_vma && adjust && adjust->anon_vma &&
		   vp.anon_vma != adjust->anon_vma);

	if (expanded) {
		/* Note: vma iterator must be pointing to 'start'. */
		vma_iter_config(vmg->vmi, vmg->start, vmg->end);
	} else {
		vma_iter_config(vmg->vmi, adjust->vm_start + adj_start,
				adjust->vm_end);
	}

	if (vma_iter_prealloc(vmg->vmi, vmg->vma))
		return -ENOMEM;

	vma_prepare(&vp);
	vma_adjust_trans_huge(vmg->vma, vmg->start, vmg->end, adj_start);
	vma_set_range(vmg->vma, vmg->start, vmg->end, vmg->pgoff);

	if (expanded)
		vma_iter_store(vmg->vmi, vmg->vma);

	if (adj_start) {
		adjust->vm_start += adj_start;
		adjust->vm_pgoff += PHYS_PFN(adj_start);
		if (adj_start < 0) {
			WARN_ON(expanded);
			vma_iter_store(vmg->vmi, adjust);
		}
	}

	vma_complete(&vp, vmg->vmi, vmg->vma->vm_mm);

	return 0;
}

/* We can only remove VMAs when merging if they do not have a close hook. */
static bool can_merge_remove_vma(struct vm_area_struct *vma)
{
	return !vma->vm_ops || !vma->vm_ops->close;
}

/*
 * vma_merge_existing_range - Attempt to merge VMAs based on a VMA having its
 * attributes modified.
 *
 * @vmg: Describes the modifications being made to a VMA and associated
 *       metadata.
 *
 * When the attributes of a range within a VMA change, then it might be possible
 * for immediately adjacent VMAs to be merged into that VMA due to having
 * identical properties.
 *
 * This function checks for the existence of any such mergeable VMAs and updates
 * the maple tree describing the @vmg->vma->vm_mm address space to account for
 * this, as well as any VMAs shrunk/expanded/deleted as a result of this merge.
 *
 * As part of this operation, if a merge occurs, the @vmg object will have its
 * vma, start, end, and pgoff fields modified to execute the merge. Subsequent
 * calls to this function should reset these fields.
 *
 * Returns: The merged VMA if merge succeeds, or NULL otherwise.
 *
 * ASSUMPTIONS:
 * - The caller must assign the VMA to be modifed to @vmg->vma.
 * - The caller must have set @vmg->prev to the previous VMA, if there is one.
 * - The caller must not set @vmg->next, as we determine this.
 * - The caller must hold a WRITE lock on the mm_struct->mmap_lock.
 * - vmi must be positioned within [@vmg->vma->vm_start, @vmg->vma->vm_end).
 */
static struct vm_area_struct *vma_merge_existing_range(struct vma_merge_struct *vmg)
{
	struct vm_area_struct *vma = vmg->vma;
	struct vm_area_struct *prev = vmg->prev;
	struct vm_area_struct *next, *res;
	struct vm_area_struct *anon_dup = NULL;
	struct vm_area_struct *adjust = NULL;
	unsigned long start = vmg->start;
	unsigned long end = vmg->end;
	bool left_side = vma && start == vma->vm_start;
	bool right_side = vma && end == vma->vm_end;
	int err = 0;
	long adj_start = 0;
	bool merge_will_delete_vma, merge_will_delete_next;
	bool merge_left, merge_right, merge_both;
	bool expanded;

	mmap_assert_write_locked(vmg->mm);
	VM_WARN_ON(!vma); /* We are modifying a VMA, so caller must specify. */
	VM_WARN_ON(vmg->next); /* We set this. */
	VM_WARN_ON(prev && start <= prev->vm_start);
	VM_WARN_ON(start >= end);
	/*
	 * If vma == prev, then we are offset into a VMA. Otherwise, if we are
	 * not, we must span a portion of the VMA.
	 */
	VM_WARN_ON(vma && ((vma != prev && vmg->start != vma->vm_start) ||
			   vmg->end > vma->vm_end));
	/* The vmi must be positioned within vmg->vma. */
	VM_WARN_ON(vma && !(vma_iter_addr(vmg->vmi) >= vma->vm_start &&
			    vma_iter_addr(vmg->vmi) < vma->vm_end));

	vmg->state = VMA_MERGE_NOMERGE;

	/*
	 * If a special mapping or if the range being modified is neither at the
	 * furthermost left or right side of the VMA, then we have no chance of
	 * merging and should abort.
	 */
	if (vmg->flags & VM_SPECIAL || (!left_side && !right_side))
		return NULL;

	if (left_side)
		merge_left = can_vma_merge_left(vmg);
	else
		merge_left = false;

	if (right_side) {
		next = vmg->next = vma_iter_next_range(vmg->vmi);
		vma_iter_prev_range(vmg->vmi);

		merge_right = can_vma_merge_right(vmg, merge_left);
	} else {
		merge_right = false;
		next = NULL;
	}

	if (merge_left)		/* If merging prev, position iterator there. */
		vma_prev(vmg->vmi);
	else if (!merge_right)	/* If we have nothing to merge, abort. */
		return NULL;

	merge_both = merge_left && merge_right;
	/* If we span the entire VMA, a merge implies it will be deleted. */
	merge_will_delete_vma = left_side && right_side;

	/*
	 * If we need to remove vma in its entirety but are unable to do so,
	 * we have no sensible recourse but to abort the merge.
	 */
	if (merge_will_delete_vma && !can_merge_remove_vma(vma))
		return NULL;

	/*
	 * If we merge both VMAs, then next is also deleted. This implies
	 * merge_will_delete_vma also.
	 */
	merge_will_delete_next = merge_both;

	/*
	 * If we cannot delete next, then we can reduce the operation to merging
	 * prev and vma (thereby deleting vma).
	 */
	if (merge_will_delete_next && !can_merge_remove_vma(next)) {
		merge_will_delete_next = false;
		merge_right = false;
		merge_both = false;
	}

	/* No matter what happens, we will be adjusting vma. */
	vma_start_write(vma);

	if (merge_left)
		vma_start_write(prev);

	if (merge_right)
		vma_start_write(next);

	if (merge_both) {
		/*
		 *         |<----->|
		 * |-------*********-------|
		 *   prev     vma     next
		 *  extend   delete  delete
		 */

		vmg->vma = prev;
		vmg->start = prev->vm_start;
		vmg->end = next->vm_end;
		vmg->pgoff = prev->vm_pgoff;

		/*
		 * We already ensured anon_vma compatibility above, so now it's
		 * simply a case of, if prev has no anon_vma object, which of
		 * next or vma contains the anon_vma we must duplicate.
		 */
		err = dup_anon_vma(prev, next->anon_vma ? next : vma, &anon_dup);
	} else if (merge_left) {
		/*
		 *         |<----->| OR
		 *         |<--------->|
		 * |-------*************
		 *   prev       vma
		 *  extend shrink/delete
		 */

		vmg->vma = prev;
		vmg->start = prev->vm_start;
		vmg->pgoff = prev->vm_pgoff;

		if (!merge_will_delete_vma) {
			adjust = vma;
			adj_start = vmg->end - vma->vm_start;
		}

		err = dup_anon_vma(prev, vma, &anon_dup);
	} else { /* merge_right */
		/*
		 *     |<----->| OR
		 * |<--------->|
		 * *************-------|
		 *      vma       next
		 * shrink/delete extend
		 */

		pgoff_t pglen = PHYS_PFN(vmg->end - vmg->start);

		VM_WARN_ON(!merge_right);
		/* If we are offset into a VMA, then prev must be vma. */
		VM_WARN_ON(vmg->start > vma->vm_start && prev && vma != prev);

		if (merge_will_delete_vma) {
			vmg->vma = next;
			vmg->end = next->vm_end;
			vmg->pgoff = next->vm_pgoff - pglen;
		} else {
			/*
			 * We shrink vma and expand next.
			 *
			 * IMPORTANT: This is the ONLY case where the final
			 * merged VMA is NOT vmg->vma, but rather vmg->next.
			 */

			vmg->start = vma->vm_start;
			vmg->end = start;
			vmg->pgoff = vma->vm_pgoff;

			adjust = next;
			adj_start = -(vma->vm_end - start);
		}

		err = dup_anon_vma(next, vma, &anon_dup);
	}

	if (err)
		goto abort;

	/*
	 * In nearly all cases, we expand vmg->vma. There is one exception -
	 * merge_right where we partially span the VMA. In this case we shrink
	 * the end of vmg->vma and adjust the start of vmg->next accordingly.
	 */
	expanded = !merge_right || merge_will_delete_vma;

	if (commit_merge(vmg, adjust,
			 merge_will_delete_vma ? vma : NULL,
			 merge_will_delete_next ? next : NULL,
			 adj_start, expanded)) {
		if (anon_dup)
			unlink_anon_vmas(anon_dup);

		vmg->state = VMA_MERGE_ERROR_NOMEM;
		return NULL;
	}

	res = merge_left ? prev : next;
	khugepaged_enter_vma(res, vmg->flags);

	vmg->state = VMA_MERGE_SUCCESS;
	return res;

abort:
	vma_iter_set(vmg->vmi, start);
	vma_iter_load(vmg->vmi);
	vmg->state = VMA_MERGE_ERROR_NOMEM;
	return NULL;
}

/*
 * vma_merge_new_range - Attempt to merge a new VMA into address space
 *
 * @vmg: Describes the VMA we are adding, in the range @vmg->start to @vmg->end
 *       (exclusive), which we try to merge with any adjacent VMAs if possible.
 *
 * We are about to add a VMA to the address space starting at @vmg->start and
 * ending at @vmg->end. There are three different possible scenarios:
 *
 * 1. There is a VMA with identical properties immediately adjacent to the
 *    proposed new VMA [@vmg->start, @vmg->end) either before or after it -
 *    EXPAND that VMA:
 *
 * Proposed:       |-----|  or  |-----|
 * Existing:  |----|                  |----|
 *
 * 2. There are VMAs with identical properties immediately adjacent to the
 *    proposed new VMA [@vmg->start, @vmg->end) both before AND after it -
 *    EXPAND the former and REMOVE the latter:
 *
 * Proposed:       |-----|
 * Existing:  |----|     |----|
 *
 * 3. There are no VMAs immediately adjacent to the proposed new VMA or those
 *    VMAs do not have identical attributes - NO MERGE POSSIBLE.
 *
 * In instances where we can merge, this function returns the expanded VMA which
 * will have its range adjusted accordingly and the underlying maple tree also
 * adjusted.
 *
 * Returns: In instances where no merge was possible, NULL. Otherwise, a pointer
 *          to the VMA we expanded.
 *
 * This function adjusts @vmg to provide @vmg->next if not already specified,
 * and adjusts [@vmg->start, @vmg->end) to span the expanded range.
 *
 * ASSUMPTIONS:
 * - The caller must hold a WRITE lock on the mm_struct->mmap_lock.
 * - The caller must have determined that [@vmg->start, @vmg->end) is empty,
     other than VMAs that will be unmapped should the operation succeed.
 * - The caller must have specified the previous vma in @vmg->prev.
 * - The caller must have specified the next vma in @vmg->next.
 * - The caller must have positioned the vmi at or before the gap.
 */
struct vm_area_struct *vma_merge_new_range(struct vma_merge_struct *vmg)
{
	struct vm_area_struct *prev = vmg->prev;
	struct vm_area_struct *next = vmg->next;
	unsigned long end = vmg->end;
	bool can_merge_left, can_merge_right;
	bool just_expand = vmg->merge_flags & VMG_FLAG_JUST_EXPAND;

	mmap_assert_write_locked(vmg->mm);
	VM_WARN_ON(vmg->vma);
	/* vmi must point at or before the gap. */
	VM_WARN_ON(vma_iter_addr(vmg->vmi) > end);

	vmg->state = VMA_MERGE_NOMERGE;

	/* Special VMAs are unmergeable, also if no prev/next. */
	if ((vmg->flags & VM_SPECIAL) || (!prev && !next))
		return NULL;

	can_merge_left = can_vma_merge_left(vmg);
	can_merge_right = !just_expand && can_vma_merge_right(vmg, can_merge_left);

	/* If we can merge with the next VMA, adjust vmg accordingly. */
	if (can_merge_right) {
		vmg->end = next->vm_end;
		vmg->vma = next;
	}

	/* If we can merge with the previous VMA, adjust vmg accordingly. */
	if (can_merge_left) {
		vmg->start = prev->vm_start;
		vmg->vma = prev;
		vmg->pgoff = prev->vm_pgoff;

		/*
		 * If this merge would result in removal of the next VMA but we
		 * are not permitted to do so, reduce the operation to merging
		 * prev and vma.
		 */
		if (can_merge_right && !can_merge_remove_vma(next))
			vmg->end = end;

		/* In expand-only case we are already positioned at prev. */
		if (!just_expand) {
			/* Equivalent to going to the previous range. */
			vma_prev(vmg->vmi);
		}
	}

	/*
	 * Now try to expand adjacent VMA(s). This takes care of removing the
	 * following VMA if we have VMAs on both sides.
	 */
	if (vmg->vma && !vma_expand(vmg)) {
		khugepaged_enter_vma(vmg->vma, vmg->flags);
		vmg->state = VMA_MERGE_SUCCESS;
		return vmg->vma;
	}

	return NULL;
}

/*
 * vma_expand - Expand an existing VMA
 *
 * @vmg: Describes a VMA expansion operation.
 *
 * Expand @vma to vmg->start and vmg->end.  Can expand off the start and end.
 * Will expand over vmg->next if it's different from vmg->vma and vmg->end ==
 * vmg->next->vm_end.  Checking if the vmg->vma can expand and merge with
 * vmg->next needs to be handled by the caller.
 *
 * Returns: 0 on success.
 *
 * ASSUMPTIONS:
 * - The caller must hold a WRITE lock on vmg->vma->mm->mmap_lock.
 * - The caller must have set @vmg->vma and @vmg->next.
 */
int vma_expand(struct vma_merge_struct *vmg)
{
	struct vm_area_struct *anon_dup = NULL;
	bool remove_next = false;
	struct vm_area_struct *vma = vmg->vma;
	struct vm_area_struct *next = vmg->next;

	mmap_assert_write_locked(vmg->mm);

	vma_start_write(vma);
	if (next && (vma != next) && (vmg->end == next->vm_end)) {
		int ret;

		remove_next = true;
		/* This should already have been checked by this point. */
		VM_WARN_ON(!can_merge_remove_vma(next));
		vma_start_write(next);
		ret = dup_anon_vma(vma, next, &anon_dup);
		if (ret)
			return ret;
	}

	/* Not merging but overwriting any part of next is not handled. */
	VM_WARN_ON(next && !remove_next &&
		  next != vma && vmg->end > next->vm_start);
	/* Only handles expanding */
	VM_WARN_ON(vma->vm_start < vmg->start || vma->vm_end > vmg->end);

	if (commit_merge(vmg, NULL, remove_next ? next : NULL, NULL, 0, true))
		goto nomem;

	return 0;

nomem:
	vmg->state = VMA_MERGE_ERROR_NOMEM;
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
	validate_mm(vma->vm_mm);
	return 0;
}

static inline void vms_clear_ptes(struct vma_munmap_struct *vms,
		    struct ma_state *mas_detach, bool mm_wr_locked)
{
	struct mmu_gather tlb;

	if (!vms->clear_ptes) /* Nothing to do */
		return;

	/*
	 * We can free page tables without write-locking mmap_lock because VMAs
	 * were isolated before we downgraded mmap_lock.
	 */
	mas_set(mas_detach, 1);
	lru_add_drain();
	tlb_gather_mmu(&tlb, vms->vma->vm_mm);
	update_hiwater_rss(vms->vma->vm_mm);
	unmap_vmas(&tlb, mas_detach, vms->vma, vms->start, vms->end,
		   vms->vma_count, mm_wr_locked);

	mas_set(mas_detach, 1);
	/* start and end may be different if there is no prev or next vma. */
	free_pgtables(&tlb, mas_detach, vms->vma, vms->unmap_start,
		      vms->unmap_end, mm_wr_locked);
	tlb_finish_mmu(&tlb);
	vms->clear_ptes = false;
}

static void vms_clean_up_area(struct vma_munmap_struct *vms,
		struct ma_state *mas_detach)
{
	struct vm_area_struct *vma;

	if (!vms->nr_pages)
		return;

	vms_clear_ptes(vms, mas_detach, true);
	mas_set(mas_detach, 0);
	mas_for_each(mas_detach, vma, ULONG_MAX)
		vma_close(vma);
}

/*
 * vms_complete_munmap_vmas() - Finish the munmap() operation
 * @vms: The vma munmap struct
 * @mas_detach: The maple state of the detached vmas
 *
 * This updates the mm_struct, unmaps the region, frees the resources
 * used for the munmap() and may downgrade the lock - if requested.  Everything
 * needed to be done once the vma maple tree is updated.
 */
static void vms_complete_munmap_vmas(struct vma_munmap_struct *vms,
		struct ma_state *mas_detach)
{
	struct vm_area_struct *vma;
	struct mm_struct *mm;

	mm = current->mm;
	mm->map_count -= vms->vma_count;
	mm->locked_vm -= vms->locked_vm;
	if (vms->unlock)
		mmap_write_downgrade(mm);

	if (!vms->nr_pages)
		return;

	vms_clear_ptes(vms, mas_detach, !vms->unlock);
	/* Update high watermark before we lower total_vm */
	update_hiwater_vm(mm);
	/* Stat accounting */
	WRITE_ONCE(mm->total_vm, READ_ONCE(mm->total_vm) - vms->nr_pages);
	/* Paranoid bookkeeping */
	VM_WARN_ON(vms->exec_vm > mm->exec_vm);
	VM_WARN_ON(vms->stack_vm > mm->stack_vm);
	VM_WARN_ON(vms->data_vm > mm->data_vm);
	mm->exec_vm -= vms->exec_vm;
	mm->stack_vm -= vms->stack_vm;
	mm->data_vm -= vms->data_vm;

	/* Remove and clean up vmas */
	mas_set(mas_detach, 0);
	mas_for_each(mas_detach, vma, ULONG_MAX)
		remove_vma(vma, /* unreachable = */ false);

	vm_unacct_memory(vms->nr_accounted);
	validate_mm(mm);
	if (vms->unlock)
		mmap_read_unlock(mm);

	__mt_destroy(mas_detach->tree);
}

/*
 * reattach_vmas() - Undo any munmap work and free resources
 * @mas_detach: The maple state with the detached maple tree
 *
 * Reattach any detached vmas and free up the maple tree used to track the vmas.
 */
static void reattach_vmas(struct ma_state *mas_detach)
{
	struct vm_area_struct *vma;

	mas_set(mas_detach, 0);
	mas_for_each(mas_detach, vma, ULONG_MAX)
		vma_mark_detached(vma, false);

	__mt_destroy(mas_detach->tree);
}

/*
 * vms_gather_munmap_vmas() - Put all VMAs within a range into a maple tree
 * for removal at a later date.  Handles splitting first and last if necessary
 * and marking the vmas as isolated.
 *
 * @vms: The vma munmap struct
 * @mas_detach: The maple state tracking the detached tree
 *
 * Return: 0 on success, error otherwise
 */
static int vms_gather_munmap_vmas(struct vma_munmap_struct *vms,
		struct ma_state *mas_detach)
{
	struct vm_area_struct *next = NULL;
	int error;

	/*
	 * If we need to split any vma, do it now to save pain later.
	 * Does it split the first one?
	 */
	if (vms->start > vms->vma->vm_start) {

		/*
		 * Make sure that map_count on return from munmap() will
		 * not exceed its limit; but let map_count go just above
		 * its limit temporarily, to help free resources as expected.
		 */
		if (vms->end < vms->vma->vm_end &&
		    vms->vma->vm_mm->map_count >= sysctl_max_map_count) {
			error = -ENOMEM;
			goto map_count_exceeded;
		}

		/* Don't bother splitting the VMA if we can't unmap it anyway */
		if (!can_modify_vma(vms->vma)) {
			error = -EPERM;
			goto start_split_failed;
		}

		error = __split_vma(vms->vmi, vms->vma, vms->start, 1);
		if (error)
			goto start_split_failed;
	}
	vms->prev = vma_prev(vms->vmi);
	if (vms->prev)
		vms->unmap_start = vms->prev->vm_end;

	/*
	 * Detach a range of VMAs from the mm. Using next as a temp variable as
	 * it is always overwritten.
	 */
	for_each_vma_range(*(vms->vmi), next, vms->end) {
		long nrpages;

		if (!can_modify_vma(next)) {
			error = -EPERM;
			goto modify_vma_failed;
		}
		/* Does it split the end? */
		if (next->vm_end > vms->end) {
			error = __split_vma(vms->vmi, next, vms->end, 0);
			if (error)
				goto end_split_failed;
		}
		vma_start_write(next);
		mas_set(mas_detach, vms->vma_count++);
		error = mas_store_gfp(mas_detach, next, GFP_KERNEL);
		if (error)
			goto munmap_gather_failed;

		vma_mark_detached(next, true);
		nrpages = vma_pages(next);

		vms->nr_pages += nrpages;
		if (next->vm_flags & VM_LOCKED)
			vms->locked_vm += nrpages;

		if (next->vm_flags & VM_ACCOUNT)
			vms->nr_accounted += nrpages;

		if (is_exec_mapping(next->vm_flags))
			vms->exec_vm += nrpages;
		else if (is_stack_mapping(next->vm_flags))
			vms->stack_vm += nrpages;
		else if (is_data_mapping(next->vm_flags))
			vms->data_vm += nrpages;

		if (vms->uf) {
			/*
			 * If userfaultfd_unmap_prep returns an error the vmas
			 * will remain split, but userland will get a
			 * highly unexpected error anyway. This is no
			 * different than the case where the first of the two
			 * __split_vma fails, but we don't undo the first
			 * split, despite we could. This is unlikely enough
			 * failure that it's not worth optimizing it for.
			 */
			error = userfaultfd_unmap_prep(next, vms->start,
						       vms->end, vms->uf);
			if (error)
				goto userfaultfd_error;
		}
#ifdef CONFIG_DEBUG_VM_MAPLE_TREE
		BUG_ON(next->vm_start < vms->start);
		BUG_ON(next->vm_start > vms->end);
#endif
	}

	vms->next = vma_next(vms->vmi);
	if (vms->next)
		vms->unmap_end = vms->next->vm_start;

#if defined(CONFIG_DEBUG_VM_MAPLE_TREE)
	/* Make sure no VMAs are about to be lost. */
	{
		MA_STATE(test, mas_detach->tree, 0, 0);
		struct vm_area_struct *vma_mas, *vma_test;
		int test_count = 0;

		vma_iter_set(vms->vmi, vms->start);
		rcu_read_lock();
		vma_test = mas_find(&test, vms->vma_count - 1);
		for_each_vma_range(*(vms->vmi), vma_mas, vms->end) {
			BUG_ON(vma_mas != vma_test);
			test_count++;
			vma_test = mas_next(&test, vms->vma_count - 1);
		}
		rcu_read_unlock();
		BUG_ON(vms->vma_count != test_count);
	}
#endif

	while (vma_iter_addr(vms->vmi) > vms->start)
		vma_iter_prev_range(vms->vmi);

	vms->clear_ptes = true;
	return 0;

userfaultfd_error:
munmap_gather_failed:
end_split_failed:
modify_vma_failed:
	reattach_vmas(mas_detach);
start_split_failed:
map_count_exceeded:
	return error;
}

/*
 * init_vma_munmap() - Initializer wrapper for vma_munmap_struct
 * @vms: The vma munmap struct
 * @vmi: The vma iterator
 * @vma: The first vm_area_struct to munmap
 * @start: The aligned start address to munmap
 * @end: The aligned end address to munmap
 * @uf: The userfaultfd list_head
 * @unlock: Unlock after the operation.  Only unlocked on success
 */
static void init_vma_munmap(struct vma_munmap_struct *vms,
		struct vma_iterator *vmi, struct vm_area_struct *vma,
		unsigned long start, unsigned long end, struct list_head *uf,
		bool unlock)
{
	vms->vmi = vmi;
	vms->vma = vma;
	if (vma) {
		vms->start = start;
		vms->end = end;
	} else {
		vms->start = vms->end = 0;
	}
	vms->unlock = unlock;
	vms->uf = uf;
	vms->vma_count = 0;
	vms->nr_pages = vms->locked_vm = vms->nr_accounted = 0;
	vms->exec_vm = vms->stack_vm = vms->data_vm = 0;
	vms->unmap_start = FIRST_USER_ADDRESS;
	vms->unmap_end = USER_PGTABLES_CEILING;
	vms->clear_ptes = false;
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
int do_vmi_align_munmap(struct vma_iterator *vmi, struct vm_area_struct *vma,
		struct mm_struct *mm, unsigned long start, unsigned long end,
		struct list_head *uf, bool unlock)
{
	struct maple_tree mt_detach;
	MA_STATE(mas_detach, &mt_detach, 0, 0);
	mt_init_flags(&mt_detach, vmi->mas.tree->ma_flags & MT_FLAGS_LOCK_MASK);
	mt_on_stack(mt_detach);
	struct vma_munmap_struct vms;
	int error;

	init_vma_munmap(&vms, vmi, vma, start, end, uf, unlock);
	error = vms_gather_munmap_vmas(&vms, &mas_detach);
	if (error)
		goto gather_failed;

	error = vma_iter_clear_gfp(vmi, start, end, GFP_KERNEL);
	if (error)
		goto clear_tree_failed;

	/* Point of no return */
	vms_complete_munmap_vmas(&vms, &mas_detach);
	return 0;

clear_tree_failed:
	reattach_vmas(&mas_detach);
gather_failed:
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
static struct vm_area_struct *vma_modify(struct vma_merge_struct *vmg)
{
	struct vm_area_struct *vma = vmg->vma;
	struct vm_area_struct *merged;

	/* First, try to merge. */
	merged = vma_merge_existing_range(vmg);
	if (merged)
		return merged;

	/* Split any preceding portion of the VMA. */
	if (vma->vm_start < vmg->start) {
		int err = split_vma(vmg->vmi, vma, vmg->start, 1);

		if (err)
			return ERR_PTR(err);
	}

	/* Split any trailing portion of the VMA. */
	if (vma->vm_end > vmg->end) {
		int err = split_vma(vmg->vmi, vma, vmg->end, 0);

		if (err)
			return ERR_PTR(err);
	}

	return vma;
}

struct vm_area_struct *vma_modify_flags(
	struct vma_iterator *vmi, struct vm_area_struct *prev,
	struct vm_area_struct *vma, unsigned long start, unsigned long end,
	unsigned long new_flags)
{
	VMG_VMA_STATE(vmg, vmi, prev, vma, start, end);

	vmg.flags = new_flags;

	return vma_modify(&vmg);
}

struct vm_area_struct
*vma_modify_flags_name(struct vma_iterator *vmi,
		       struct vm_area_struct *prev,
		       struct vm_area_struct *vma,
		       unsigned long start,
		       unsigned long end,
		       unsigned long new_flags,
		       struct anon_vma_name *new_name)
{
	VMG_VMA_STATE(vmg, vmi, prev, vma, start, end);

	vmg.flags = new_flags;
	vmg.anon_name = new_name;

	return vma_modify(&vmg);
}

struct vm_area_struct
*vma_modify_policy(struct vma_iterator *vmi,
		   struct vm_area_struct *prev,
		   struct vm_area_struct *vma,
		   unsigned long start, unsigned long end,
		   struct mempolicy *new_pol)
{
	VMG_VMA_STATE(vmg, vmi, prev, vma, start, end);

	vmg.policy = new_pol;

	return vma_modify(&vmg);
}

struct vm_area_struct
*vma_modify_flags_uffd(struct vma_iterator *vmi,
		       struct vm_area_struct *prev,
		       struct vm_area_struct *vma,
		       unsigned long start, unsigned long end,
		       unsigned long new_flags,
		       struct vm_userfaultfd_ctx new_ctx)
{
	VMG_VMA_STATE(vmg, vmi, prev, vma, start, end);

	vmg.flags = new_flags;
	vmg.uffd_ctx = new_ctx;

	return vma_modify(&vmg);
}

/*
 * Expand vma by delta bytes, potentially merging with an immediately adjacent
 * VMA with identical properties.
 */
struct vm_area_struct *vma_merge_extend(struct vma_iterator *vmi,
					struct vm_area_struct *vma,
					unsigned long delta)
{
	VMG_VMA_STATE(vmg, vmi, vma, vma, vma->vm_end, vma->vm_end + delta);

	vmg.next = vma_iter_next_rewind(vmi, NULL);
	vmg.vma = NULL; /* We use the VMA to populate VMG fields only. */

	return vma_merge_new_range(&vmg);
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
	struct vm_area_struct *new_vma;
	bool faulted_in_anon_vma = true;
	VMA_ITERATOR(vmi, mm, addr);
	VMG_VMA_STATE(vmg, &vmi, NULL, vma, addr, addr + len);

	/*
	 * If anonymous vma has not yet been faulted, update new pgoff
	 * to match new location, to increase its chance of merging.
	 */
	if (unlikely(vma_is_anonymous(vma) && !vma->anon_vma)) {
		pgoff = addr >> PAGE_SHIFT;
		faulted_in_anon_vma = false;
	}

	new_vma = find_vma_prev(mm, addr, &vmg.prev);
	if (new_vma && new_vma->vm_start < addr + len)
		return NULL;	/* should never get here */

	vmg.vma = NULL; /* New VMA range. */
	vmg.pgoff = pgoff;
	vmg.next = vma_iter_next_rewind(&vmi, NULL);
	new_vma = vma_merge_new_range(&vmg);

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
	vma_close(new_vma);

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

/*
 * We account for memory if it's a private writeable mapping,
 * not hugepages and VM_NORESERVE wasn't set.
 */
static bool accountable_mapping(struct file *file, vm_flags_t vm_flags)
{
	/*
	 * hugetlb has its own accounting separate from the core VM
	 * VM_HUGETLB may not be set yet so we cannot check for that flag.
	 */
	if (file && is_file_hugepages(file))
		return false;

	return (vm_flags & (VM_NORESERVE | VM_SHARED | VM_WRITE)) == VM_WRITE;
}

/*
 * vms_abort_munmap_vmas() - Undo as much as possible from an aborted munmap()
 * operation.
 * @vms: The vma unmap structure
 * @mas_detach: The maple state with the detached maple tree
 *
 * Reattach any detached vmas, free up the maple tree used to track the vmas.
 * If that's not possible because the ptes are cleared (and vm_ops->closed() may
 * have been called), then a NULL is written over the vmas and the vmas are
 * removed (munmap() completed).
 */
static void vms_abort_munmap_vmas(struct vma_munmap_struct *vms,
		struct ma_state *mas_detach)
{
	struct ma_state *mas = &vms->vmi->mas;

	if (!vms->nr_pages)
		return;

	if (vms->clear_ptes)
		return reattach_vmas(mas_detach);

	/*
	 * Aborting cannot just call the vm_ops open() because they are often
	 * not symmetrical and state data has been lost.  Resort to the old
	 * failure method of leaving a gap where the MAP_FIXED mapping failed.
	 */
	mas_set_range(mas, vms->start, vms->end - 1);
	mas_store_gfp(mas, NULL, GFP_KERNEL|__GFP_NOFAIL);
	/* Clean up the insertion of the unfortunate gap */
	vms_complete_munmap_vmas(vms, mas_detach);
}

/*
 * __mmap_prepare() - Prepare to gather any overlapping VMAs that need to be
 * unmapped once the map operation is completed, check limits, account mapping
 * and clean up any pre-existing VMAs.
 *
 * @map: Mapping state.
 * @uf:  Userfaultfd context list.
 *
 * Returns: 0 on success, error code otherwise.
 */
static int __mmap_prepare(struct mmap_state *map, struct list_head *uf)
{
	int error;
	struct vma_iterator *vmi = map->vmi;
	struct vma_munmap_struct *vms = &map->vms;

	/* Find the first overlapping VMA and initialise unmap state. */
	vms->vma = vma_find(vmi, map->end);
	init_vma_munmap(vms, vmi, vms->vma, map->addr, map->end, uf,
			/* unlock = */ false);

	/* OK, we have overlapping VMAs - prepare to unmap them. */
	if (vms->vma) {
		mt_init_flags(&map->mt_detach,
			      vmi->mas.tree->ma_flags & MT_FLAGS_LOCK_MASK);
		mt_on_stack(map->mt_detach);
		mas_init(&map->mas_detach, &map->mt_detach, /* addr = */ 0);
		/* Prepare to unmap any existing mapping in the area */
		error = vms_gather_munmap_vmas(vms, &map->mas_detach);
		if (error) {
			/* On error VMAs will already have been reattached. */
			vms->nr_pages = 0;
			return error;
		}

		map->next = vms->next;
		map->prev = vms->prev;
	} else {
		map->next = vma_iter_next_rewind(vmi, &map->prev);
	}

	/* Check against address space limit. */
	if (!may_expand_vm(map->mm, map->flags, map->pglen - vms->nr_pages))
		return -ENOMEM;

	/* Private writable mapping: check memory availability. */
	if (accountable_mapping(map->file, map->flags)) {
		map->charged = map->pglen;
		map->charged -= vms->nr_accounted;
		if (map->charged) {
			error = security_vm_enough_memory_mm(map->mm, map->charged);
			if (error)
				return error;
		}

		vms->nr_accounted = 0;
		map->flags |= VM_ACCOUNT;
	}

	/*
	 * Clear PTEs while the vma is still in the tree so that rmap
	 * cannot race with the freeing later in the truncate scenario.
	 * This is also needed for mmap_file(), which is why vm_ops
	 * close function is called.
	 */
	vms_clean_up_area(vms, &map->mas_detach);

	return 0;
}


static int __mmap_new_file_vma(struct mmap_state *map,
			       struct vm_area_struct *vma)
{
	struct vma_iterator *vmi = map->vmi;
	int error;

	vma->vm_file = get_file(map->file);
	error = mmap_file(vma->vm_file, vma);
	if (error) {
		fput(vma->vm_file);
		vma->vm_file = NULL;

		vma_iter_set(vmi, vma->vm_end);
		/* Undo any partial mapping done by a device driver. */
		unmap_region(&vmi->mas, vma, map->prev, map->next);

		return error;
	}

	/* Drivers cannot alter the address of the VMA. */
	WARN_ON_ONCE(map->addr != vma->vm_start);
	/*
	 * Drivers should not permit writability when previously it was
	 * disallowed.
	 */
	VM_WARN_ON_ONCE(map->flags != vma->vm_flags &&
			!(map->flags & VM_MAYWRITE) &&
			(vma->vm_flags & VM_MAYWRITE));

	/* If the flags change (and are mergeable), let's retry later. */
	map->retry_merge = vma->vm_flags != map->flags && !(vma->vm_flags & VM_SPECIAL);
	map->flags = vma->vm_flags;

	return 0;
}

/*
 * __mmap_new_vma() - Allocate a new VMA for the region, as merging was not
 * possible.
 *
 * @map:  Mapping state.
 * @vmap: Output pointer for the new VMA.
 *
 * Returns: Zero on success, or an error.
 */
static int __mmap_new_vma(struct mmap_state *map, struct vm_area_struct **vmap)
{
	struct vma_iterator *vmi = map->vmi;
	int error = 0;
	struct vm_area_struct *vma;

	/*
	 * Determine the object being mapped and call the appropriate
	 * specific mapper. the address has already been validated, but
	 * not unmapped, but the maps are removed from the list.
	 */
	vma = vm_area_alloc(map->mm);
	if (!vma)
		return -ENOMEM;

	vma_iter_config(vmi, map->addr, map->end);
	vma_set_range(vma, map->addr, map->end, map->pgoff);
	vm_flags_init(vma, map->flags);
	vma->vm_page_prot = vm_get_page_prot(map->flags);

	if (vma_iter_prealloc(vmi, vma)) {
		error = -ENOMEM;
		goto free_vma;
	}

	if (map->file)
		error = __mmap_new_file_vma(map, vma);
	else if (map->flags & VM_SHARED)
		error = shmem_zero_setup(vma);
	else
		vma_set_anonymous(vma);

	if (error)
		goto free_iter_vma;

#ifdef CONFIG_SPARC64
	/* TODO: Fix SPARC ADI! */
	WARN_ON_ONCE(!arch_validate_flags(map->flags));
#endif

	/* Lock the VMA since it is modified after insertion into VMA tree */
	vma_start_write(vma);
	vma_iter_store(vmi, vma);
	map->mm->map_count++;
	vma_link_file(vma);

	/*
	 * vma_merge_new_range() calls khugepaged_enter_vma() too, the below
	 * call covers the non-merge case.
	 */
	khugepaged_enter_vma(vma, map->flags);
	ksm_add_vma(vma);
	*vmap = vma;
	return 0;

free_iter_vma:
	vma_iter_free(vmi);
free_vma:
	vm_area_free(vma);
	return error;
}

/*
 * __mmap_complete() - Unmap any VMAs we overlap, account memory mapping
 *                     statistics, handle locking and finalise the VMA.
 *
 * @map: Mapping state.
 * @vma: Merged or newly allocated VMA for the mmap()'d region.
 */
static void __mmap_complete(struct mmap_state *map, struct vm_area_struct *vma)
{
	struct mm_struct *mm = map->mm;
	unsigned long vm_flags = vma->vm_flags;

	perf_event_mmap(vma);

	/* Unmap any existing mapping in the area. */
	vms_complete_munmap_vmas(&map->vms, &map->mas_detach);

	vm_stat_account(mm, vma->vm_flags, map->pglen);
	if (vm_flags & VM_LOCKED) {
		if ((vm_flags & VM_SPECIAL) || vma_is_dax(vma) ||
					is_vm_hugetlb_page(vma) ||
					vma == get_gate_vma(mm))
			vm_flags_clear(vma, VM_LOCKED_MASK);
		else
			mm->locked_vm += map->pglen;
	}

	if (vma->vm_file)
		uprobe_mmap(vma);

	/*
	 * New (or expanded) vma always get soft dirty status.
	 * Otherwise user-space soft-dirty page tracker won't
	 * be able to distinguish situation when vma area unmapped,
	 * then new mapped in-place (which must be aimed as
	 * a completely new data area).
	 */
	vm_flags_set(vma, VM_SOFTDIRTY);

	vma_set_page_prot(vma);
}

unsigned long __mmap_region(struct file *file, unsigned long addr,
		unsigned long len, vm_flags_t vm_flags, unsigned long pgoff,
		struct list_head *uf)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma = NULL;
	int error;
	VMA_ITERATOR(vmi, mm, addr);
	MMAP_STATE(map, mm, &vmi, addr, len, pgoff, vm_flags, file);

	error = __mmap_prepare(&map, uf);
	if (error)
		goto abort_munmap;

	/* Attempt to merge with adjacent VMAs... */
	if (map.prev || map.next) {
		VMG_MMAP_STATE(vmg, &map, /* vma = */ NULL);

		vma = vma_merge_new_range(&vmg);
	}

	/* ...but if we can't, allocate a new VMA. */
	if (!vma) {
		error = __mmap_new_vma(&map, &vma);
		if (error)
			goto unacct_error;
	}

	/* If flags changed, we might be able to merge, so try again. */
	if (map.retry_merge) {
		VMG_MMAP_STATE(vmg, &map, vma);

		vma_iter_config(map.vmi, map.addr, map.end);
		vma_merge_existing_range(&vmg);
	}

	__mmap_complete(&map, vma);

	return addr;

	/* Accounting was done by __mmap_prepare(). */
unacct_error:
	if (map.charged)
		vm_unacct_memory(map.charged);
abort_munmap:
	vms_abort_munmap_vmas(&map.vms, &map.mas_detach);
	return error;
}
