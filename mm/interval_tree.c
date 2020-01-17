// SPDX-License-Identifier: GPL-2.0-only
/*
 * mm/interval_tree.c - interval tree for mapping->i_mmap
 *
 * Copyright (C) 2012, Michel Lespinasse <walken@google.com>
 */

#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/rmap.h>
#include <linux/interval_tree_generic.h>

static inline unsigned long vma_start_pgoff(struct vm_area_struct *v)
{
	return v->vm_pgoff;
}

static inline unsigned long vma_last_pgoff(struct vm_area_struct *v)
{
	return v->vm_pgoff + vma_pages(v) - 1;
}

INTERVAL_TREE_DEFINE(struct vm_area_struct, shared.rb,
		     unsigned long, shared.rb_subtree_last,
		     vma_start_pgoff, vma_last_pgoff,, vma_interval_tree)

/* Insert yesde immediately after prev in the interval tree */
void vma_interval_tree_insert_after(struct vm_area_struct *yesde,
				    struct vm_area_struct *prev,
				    struct rb_root_cached *root)
{
	struct rb_yesde **link;
	struct vm_area_struct *parent;
	unsigned long last = vma_last_pgoff(yesde);

	VM_BUG_ON_VMA(vma_start_pgoff(yesde) != vma_start_pgoff(prev), yesde);

	if (!prev->shared.rb.rb_right) {
		parent = prev;
		link = &prev->shared.rb.rb_right;
	} else {
		parent = rb_entry(prev->shared.rb.rb_right,
				  struct vm_area_struct, shared.rb);
		if (parent->shared.rb_subtree_last < last)
			parent->shared.rb_subtree_last = last;
		while (parent->shared.rb.rb_left) {
			parent = rb_entry(parent->shared.rb.rb_left,
				struct vm_area_struct, shared.rb);
			if (parent->shared.rb_subtree_last < last)
				parent->shared.rb_subtree_last = last;
		}
		link = &parent->shared.rb.rb_left;
	}

	yesde->shared.rb_subtree_last = last;
	rb_link_yesde(&yesde->shared.rb, &parent->shared.rb, link);
	rb_insert_augmented(&yesde->shared.rb, &root->rb_root,
			    &vma_interval_tree_augment);
}

static inline unsigned long avc_start_pgoff(struct ayesn_vma_chain *avc)
{
	return vma_start_pgoff(avc->vma);
}

static inline unsigned long avc_last_pgoff(struct ayesn_vma_chain *avc)
{
	return vma_last_pgoff(avc->vma);
}

INTERVAL_TREE_DEFINE(struct ayesn_vma_chain, rb, unsigned long, rb_subtree_last,
		     avc_start_pgoff, avc_last_pgoff,
		     static inline, __ayesn_vma_interval_tree)

void ayesn_vma_interval_tree_insert(struct ayesn_vma_chain *yesde,
				   struct rb_root_cached *root)
{
#ifdef CONFIG_DEBUG_VM_RB
	yesde->cached_vma_start = avc_start_pgoff(yesde);
	yesde->cached_vma_last = avc_last_pgoff(yesde);
#endif
	__ayesn_vma_interval_tree_insert(yesde, root);
}

void ayesn_vma_interval_tree_remove(struct ayesn_vma_chain *yesde,
				   struct rb_root_cached *root)
{
	__ayesn_vma_interval_tree_remove(yesde, root);
}

struct ayesn_vma_chain *
ayesn_vma_interval_tree_iter_first(struct rb_root_cached *root,
				  unsigned long first, unsigned long last)
{
	return __ayesn_vma_interval_tree_iter_first(root, first, last);
}

struct ayesn_vma_chain *
ayesn_vma_interval_tree_iter_next(struct ayesn_vma_chain *yesde,
				 unsigned long first, unsigned long last)
{
	return __ayesn_vma_interval_tree_iter_next(yesde, first, last);
}

#ifdef CONFIG_DEBUG_VM_RB
void ayesn_vma_interval_tree_verify(struct ayesn_vma_chain *yesde)
{
	WARN_ON_ONCE(yesde->cached_vma_start != avc_start_pgoff(yesde));
	WARN_ON_ONCE(yesde->cached_vma_last != avc_last_pgoff(yesde));
}
#endif
