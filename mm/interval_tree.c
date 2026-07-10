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
	return vma_start_pgoff(avc->vma);
}

static pgoff_t avc_last_pgoff(struct anon_vma_chain *avc)
{
	return vma_last_pgoff(avc->vma);
}

INTERVAL_TREE_DEFINE(struct anon_vma_chain, rb, pgoff_t, rb_subtree_last,
		     avc_start_pgoff, avc_last_pgoff,
		     static, __anon_vma_interval_tree)

void anon_vma_interval_tree_insert(struct anon_vma_chain *avc,
				   struct anon_vma *anon_vma)
{
#ifdef CONFIG_DEBUG_VM_RB
	avc->cached_vma_start = avc_start_pgoff(avc);
	avc->cached_vma_last = avc_last_pgoff(avc);
#endif
	__anon_vma_interval_tree_insert(avc, &anon_vma->rb_root);
}

void anon_vma_interval_tree_remove(struct anon_vma_chain *avc,
				   struct anon_vma *anon_vma)
{
	__anon_vma_interval_tree_remove(avc, &anon_vma->rb_root);
}

struct anon_vma_chain *
anon_vma_interval_tree_iter_first(struct anon_vma *anon_vma,
				  pgoff_t pgoff_start, pgoff_t pgoff_last)
{
	return __anon_vma_interval_tree_iter_first(&anon_vma->rb_root,
						   pgoff_start, pgoff_last);
}

struct anon_vma_chain *
anon_vma_interval_tree_iter_next(struct anon_vma_chain *avc,
				  pgoff_t pgoff_start, pgoff_t pgoff_last)
{
	return __anon_vma_interval_tree_iter_next(avc, pgoff_start, pgoff_last);
}

#ifdef CONFIG_DEBUG_VM_RB
void anon_vma_interval_tree_verify(struct anon_vma_chain *avc)
{
	WARN_ON_ONCE(avc->cached_vma_start != avc_start_pgoff(avc));
	WARN_ON_ONCE(avc->cached_vma_last != avc_last_pgoff(avc));
}
#endif
