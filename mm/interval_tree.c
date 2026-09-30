// SPDX-License-Identifier: GPL-2.0-only
/*
 * mm/interval_tree.c - interval tree for address_space->i_mmap and
 * anon_vma->rb_root
 *
 * Copyright (C) 2012, Michel Lespinasse <walken@google.com>
 */

#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/rmap.h>
#include <linux/interval_tree_generic.h>

/* File-backed interval tree (address_space->i_mmap) */

INTERVAL_TREE_DEFINE(struct vm_area_struct, shared.rb,
		     pgoff_t, shared.rb_subtree_last,
		     vma_start_pgoff, vma_last_pgoff, static,
		     __mapping_rmap_tree)

void mapping_rmap_tree_insert(struct vm_area_struct *vma,
			      struct address_space *mapping)
{
	__mapping_rmap_tree_insert(vma, &mapping->i_mmap);
}

/* Insert vma immediately after prev in the interval tree */
void mapping_rmap_tree_insert_after(struct vm_area_struct *vma,
				    struct vm_area_struct *prev,
				    struct address_space *mapping)
{
	struct rb_node **link;
	struct vm_area_struct *parent;
	const pgoff_t pgoff_last = vma_last_pgoff(vma);

	VM_WARN_ON_ONCE_VMA(vma_start_pgoff(vma) != vma_start_pgoff(prev), vma);

	if (!prev->shared.rb.rb_right) {
		parent = prev;
		link = &prev->shared.rb.rb_right;
	} else {
		parent = rb_entry(prev->shared.rb.rb_right,
				  struct vm_area_struct, shared.rb);
		if (parent->shared.rb_subtree_last < pgoff_last)
			parent->shared.rb_subtree_last = pgoff_last;
		while (parent->shared.rb.rb_left) {
			parent = rb_entry(parent->shared.rb.rb_left,
				struct vm_area_struct, shared.rb);
			if (parent->shared.rb_subtree_last < pgoff_last)
				parent->shared.rb_subtree_last = pgoff_last;
		}
		link = &parent->shared.rb.rb_left;
	}

	vma->shared.rb_subtree_last = pgoff_last;
	rb_link_node(&vma->shared.rb, &parent->shared.rb, link);
	rb_insert_augmented(&vma->shared.rb, &mapping->i_mmap.rb_root,
			    &__mapping_rmap_tree_augment);
}

void mapping_rmap_tree_remove(struct vm_area_struct *vma,
			      struct address_space *mapping)
{
	__mapping_rmap_tree_remove(vma, &mapping->i_mmap);
}

static void mapping_rmap_tree_update_inplace(struct vm_area_struct *vma)
{
	/* Propagate all the way up the tree. */
	__mapping_rmap_tree_augment.propagate(&vma->shared.rb, NULL);
}

/**
 * mapping_rmap_tree_pre_update() - Prepare the file rmap tree for a change to
 * be made to @vma.
 * @vma: The VMA about to be updated.
 * @mapping: The file rmap to which @vma belongs.
 * @pgoff_unchanged: Whether @vma's page offset will remain unchanged.
 *
 * The file rmap lock must be held across the entire update.
 */
void mapping_rmap_tree_pre_update(struct vm_area_struct *vma,
				  struct address_space *mapping,
				  bool pgoff_unchanged)
{
	/* If the pgoff has changed, then remove and reinsert afterwards. */
	if (!pgoff_unchanged)
		mapping_rmap_tree_remove(vma, mapping);
}

/**
 * mapping_rmap_tree_post_update() - Update the file rmap tree to reflect a
 * change that has been made to @vma.
 * @vma: The VMA that has been updated.
 * @mapping: The file rmap to which @vma belongs.
 * @pgoff_unchanged: Whether @vma's page offset remained unchanged.
 *
 * mapping_rmap_tree_pre_update() must have been called prior to this.
 *
 * The file rmap lock must be held across the entire update.
 */
void mapping_rmap_tree_post_update(struct vm_area_struct *vma,
				   struct address_space *mapping,
				   bool pgoff_unchanged)
{
	if (pgoff_unchanged)
		mapping_rmap_tree_update_inplace(vma);
	else
		mapping_rmap_tree_insert(vma, mapping);
}

struct vm_area_struct *
mapping_rmap_tree_iter_first(struct address_space *mapping,
			     pgoff_t pgoff_start, pgoff_t pgoff_last)
{
	return __mapping_rmap_tree_iter_first(&mapping->i_mmap,
					      pgoff_start, pgoff_last);
}

struct vm_area_struct *
mapping_rmap_tree_iter_next(struct vm_area_struct *vma,
			    pgoff_t pgoff_start, pgoff_t pgoff_last)
{
	return __mapping_rmap_tree_iter_next(vma, pgoff_start, pgoff_last);
}

/* Anonymous interval tree (anon_vma->rb_root) */

static pgoff_t avc_start_pgoff(struct anon_vma_chain *avc)
{
	return vma_start_anon_pgoff(avc->vma);
}

static pgoff_t avc_last_pgoff(struct anon_vma_chain *avc)
{
	return vma_last_anon_pgoff(avc->vma);
}

INTERVAL_TREE_DEFINE(struct anon_vma_chain, rb, pgoff_t, rb_subtree_last,
		     avc_start_pgoff, avc_last_pgoff,
		     static, __anon_rmap_tree)

void anon_rmap_tree_insert(struct anon_vma_chain *avc,
			   struct anon_vma *anon_vma)
{
#ifdef CONFIG_DEBUG_VM_RB
	avc->cached_vma_start = avc_start_pgoff(avc);
	avc->cached_vma_last = avc_last_pgoff(avc);
#endif
	__anon_rmap_tree_insert(avc, &anon_vma->rb_root);
}

void anon_rmap_tree_remove(struct anon_vma_chain *avc,
			   struct anon_vma *anon_vma)
{
	__anon_rmap_tree_remove(avc, &anon_vma->rb_root);
}

static void anon_rmap_tree_update_inplace(struct anon_vma_chain *avc)
{
#ifdef CONFIG_DEBUG_VM_RB
	avc->cached_vma_last = avc_last_pgoff(avc);
#endif
	/* Propagate all the way up the tree. */
	__anon_rmap_tree_augment.propagate(&avc->rb, NULL);
}

/**
 * anon_rmap_tree_pre_update_vma() - Prepare the anon rmap trees for a change
 * to be made to @vma.
 * @vma: The VMA about to be updated, which has an anon rmap assigned and is
 *       already inserted on its interval trees.
 * @anon_pgoff_unchanged: Whether @vma's anonymous page offset will remain
 *                        unchanged.
 *
 * The anon rmap lock must be held across the entire update.
 */
void anon_rmap_tree_pre_update_vma(struct vm_area_struct *vma,
				   bool anon_pgoff_unchanged)
{
	struct anon_vma_chain *avc;

	if (anon_pgoff_unchanged)
		return;

	/* If the pgoff has changed, then remove and reinsert afterwards. */
	list_for_each_entry(avc, &vma->anon_vma_chain, same_vma)
		anon_rmap_tree_remove(avc, avc->anon_vma);
}

/**
 * anon_rmap_tree_post_update_vma() - Update the anon rmap trees to reflect a
 * change that has been made to @vma.
 * @vma: The VMA that has been updated.
 * @anon_pgoff_unchanged: Whether @vma's anonymous page offset remained
 *                        unchanged.
 *
 * anon_rmap_tree_pre_update_vma() must have been called prior to this.
 *
 * The anon rmap lock must be held across the entire update.
 */
void anon_rmap_tree_post_update_vma(struct vm_area_struct *vma,
				    bool anon_pgoff_unchanged)
{
	struct anon_vma_chain *avc;

	list_for_each_entry(avc, &vma->anon_vma_chain, same_vma) {
		if (anon_pgoff_unchanged)
			anon_rmap_tree_update_inplace(avc);
		else
			anon_rmap_tree_insert(avc, avc->anon_vma);
	}
}

struct anon_vma_chain *
anon_rmap_tree_iter_first(struct anon_vma *anon_vma,
			  pgoff_t pgoff_start, pgoff_t pgoff_last)
{
	return __anon_rmap_tree_iter_first(&anon_vma->rb_root,
					   pgoff_start, pgoff_last);
}

struct anon_vma_chain *
anon_rmap_tree_iter_next(struct anon_vma_chain *avc,
			 pgoff_t pgoff_start, pgoff_t pgoff_last)
{
	return __anon_rmap_tree_iter_next(avc, pgoff_start, pgoff_last);
}

#ifdef CONFIG_DEBUG_VM_RB
void anon_rmap_tree_verify(struct anon_vma_chain *avc)
{
	WARN_ON_ONCE(avc->cached_vma_start != avc_start_pgoff(avc));
	WARN_ON_ONCE(avc->cached_vma_last != avc_last_pgoff(avc));
}
#endif
