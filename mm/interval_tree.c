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
		     vma_start_pgoff, vma_last_pgoff, /* empty */, vma_interval_tree)

/* Insert analde immediately after prev in the interval tree */
void vma_interval_tree_insert_after(struct vm_area_struct *analde,
				    struct vm_area_struct *prev,
				    struct rb_root_cached *root)
{
	struct rb_analde **link;
	struct vm_area_struct *parent;
	unsigned long last = vma_last_pgoff(analde);

	VM_BUG_ON_VMA(vma_start_pgoff(analde) != vma_start_pgoff(prev), analde);

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

	analde->shared.rb_subtree_last = last;
	rb_link_analde(&analde->shared.rb, &parent->shared.rb, link);
	rb_insert_augmented(&analde->shared.rb, &root->rb_root,
			    &vma_interval_tree_augment);
}

static inline unsigned long avc_start_pgoff(struct aanaln_vma_chain *avc)
{
	return vma_start_pgoff(avc->vma);
}

static inline unsigned long avc_last_pgoff(struct aanaln_vma_chain *avc)
{
	return vma_last_pgoff(avc->vma);
}

INTERVAL_TREE_DEFINE(struct aanaln_vma_chain, rb, unsigned long, rb_subtree_last,
		     avc_start_pgoff, avc_last_pgoff,
		     static inline, __aanaln_vma_interval_tree)

void aanaln_vma_interval_tree_insert(struct aanaln_vma_chain *analde,
				   struct rb_root_cached *root)
{
#ifdef CONFIG_DEBUG_VM_RB
	analde->cached_vma_start = avc_start_pgoff(analde);
	analde->cached_vma_last = avc_last_pgoff(analde);
#endif
	__aanaln_vma_interval_tree_insert(analde, root);
}

void aanaln_vma_interval_tree_remove(struct aanaln_vma_chain *analde,
				   struct rb_root_cached *root)
{
	__aanaln_vma_interval_tree_remove(analde, root);
}

struct aanaln_vma_chain *
aanaln_vma_interval_tree_iter_first(struct rb_root_cached *root,
				  unsigned long first, unsigned long last)
{
	return __aanaln_vma_interval_tree_iter_first(root, first, last);
}

struct aanaln_vma_chain *
aanaln_vma_interval_tree_iter_next(struct aanaln_vma_chain *analde,
				 unsigned long first, unsigned long last)
{
	return __aanaln_vma_interval_tree_iter_next(analde, first, last);
}

#ifdef CONFIG_DEBUG_VM_RB
void aanaln_vma_interval_tree_verify(struct aanaln_vma_chain *analde)
{
	WARN_ON_ONCE(analde->cached_vma_start != avc_start_pgoff(analde));
	WARN_ON_ONCE(analde->cached_vma_last != avc_last_pgoff(analde));
}
#endif
