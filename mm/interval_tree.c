/*
 * mm/interval_tree.c - interval tree for mapping->i_mmap
 *
 * Copyright (C) 2012, Michel Lespinasse <walken@google.com>
 *
 * This file is released under the GPL v2.
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
	return v->vm_pgoff + ((v->vm_end - v->vm_start) >> PAGE_SHIFT) - 1;
}

INTERVAL_TREE_DEFINE(struct vm_area_struct, shared.linear.rb,
		     unsigned long, shared.linear.rb_subtree_last,
		     vma_start_pgoff, vma_last_pgoff,, vma_interval_tree)

/* Insert node immediately after prev in the interval tree */
void vma_interval_tree_insert_after(struct vm_area_struct *node,
				    struct vm_area_struct *prev,
				    struct rb_root *root)
{
	struct rb_node **link;
	struct vm_area_struct *parent;
	unsigned long last = vma_last_pgoff(node);

	VM_BUG_ON(vma_start_pgoff(node) != vma_start_pgoff(prev));

	if (!prev->shared.linear.rb.rb_right) {
		parent = prev;
		link = &prev->shared.linear.rb.rb_right;
	} else {
		parent = rb_entry(prev->shared.linear.rb.rb_right,
				  struct vm_area_struct, shared.linear.rb);
		if (parent->shared.linear.rb_subtree_last < last)
			parent->shared.linear.rb_subtree_last = last;
		while (parent->shared.linear.rb.rb_left) {
			parent = rb_entry(parent->shared.linear.rb.rb_left,
				struct vm_area_struct, shared.linear.rb);
			if (parent->shared.linear.rb_subtree_last < last)
				parent->shared.linear.rb_subtree_last = last;
		}
		link = &parent->shared.linear.rb.rb_left;
	}

	node->shared.linear.rb_subtree_last = last;
	rb_link_node(&node->shared.linear.rb, &parent->shared.linear.rb, link);
	rb_insert_augmented(&node->shared.linear.rb, root,
			    &vma_interval_tree_augment);
}

static inline unsigned long avc_start_pgoff(struct anon_vma_chain *avc)
{
	return vma_start_pgoff(avc->vma);
}

static inline unsigned long avc_last_pgoff(struct anon_vma_chain *avc)
{
	return vma_last_pgoff(avc->vma);
}

INTERVAL_TREE_DEFINE(struct anon_vma_chain, rb, unsigned long, rb_subtree_last,
		     avc_start_pgoff, avc_last_pgoff,, anon_vma_interval_tree)
