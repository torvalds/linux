/*
 * mm/interval_tree.c - interval tree for mapping->i_mmap
 *
 * Copyright (C) 2012, Michel Lespinasse <walken@google.com>
 *
 * This file is released under the GPL v2.
 */

#include <linux/mm.h>
#include <linux/fs.h>

#define ITSTRUCT   struct vm_area_struct
#define ITRB       shared.linear.rb
#define ITTYPE     unsigned long
#define ITSUBTREE  shared.linear.rb_subtree_last
#define ITSTART(n) ((n)->vm_pgoff)
#define ITLAST(n)  ((n)->vm_pgoff + \
		    (((n)->vm_end - (n)->vm_start) >> PAGE_SHIFT) - 1)
#define ITSTATIC
#define ITPREFIX   vma_interval_tree

#include <linux/interval_tree_tmpl.h>

/* Insert old immediately after vma in the interval tree */
void vma_interval_tree_add(struct vm_area_struct *vma,
			   struct vm_area_struct *old,
			   struct address_space *mapping)
{
	struct rb_node **link;
	struct vm_area_struct *parent;
	unsigned long last;

	if (unlikely(vma->vm_flags & VM_NONLINEAR)) {
		list_add(&vma->shared.nonlinear, &old->shared.nonlinear);
		return;
	}

	last = ITLAST(vma);

	if (!old->shared.linear.rb.rb_right) {
		parent = old;
		link = &old->shared.linear.rb.rb_right;
	} else {
		parent = rb_entry(old->shared.linear.rb.rb_right,
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

	vma->shared.linear.rb_subtree_last = last;
	rb_link_node(&vma->shared.linear.rb, &parent->shared.linear.rb, link);
	rb_insert_augmented(&vma->shared.linear.rb, &mapping->i_mmap,
			    &vma_interval_tree_augment_callbacks);
}
