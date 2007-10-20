/*
 * mm/prio_tree.c - priority search tree for mapping->i_mmap
 *
 * Copyright (C) 2004, Rajesh Venkatasubramanian <vrajesh@umich.edu>
 *
 * This file is released under the GPL v2.
 *
 * Based on the radix priority search tree proposed by Edward M. McCreight
 * SIAM Journal of Computing, vol. 14, no.2, pages 257-276, May 1985
 *
 * 02Feb2004	Initial version
 */

#include <linux/mm.h>
#include <linux/prio_tree.h>

/*
 * See lib/prio_tree.c for details on the general radix priority search tree
 * code.
 */

/*
 * The following #defines are mirrored from lib/prio_tree.c. They're only used
 * for debugging, and should be removed (along with the debugging code using
 * them) when switching also VMAs to the regular prio_tree code.
 */

#define RADIX_INDEX(vma)  ((vma)->vm_pgoff)
#define VMA_SIZE(vma)	  (((vma)->vm_end - (vma)->vm_start) >> PAGE_SHIFT)
/* avoid overflow */
#define HEAP_INDEX(vma)   ((vma)->vm_pgoff + (VMA_SIZE(vma) - 1))

/*
 * Radix priority search tree for address_space->i_mmap
 *
 * For each vma that map a unique set of file pages i.e., unique [radix_index,
 * heap_index] value, we have a corresponding priority search tree node. If
 * multiple vmas have identical [radix_index, heap_index] value, then one of
 * them is used as a tree node and others are stored in a vm_set list. The tree
 * node points to the first vma (head) of the list using vm_set.head.
 *
 * prio_tree_root
 *      |
 *      A       vm_set.head
 *     / \      /
 *    L   R -> H-I-J-K-M-N-O-P-Q-S
 *    ^   ^    <-- vm_set.list -->
 *  tree nodes
 *
 * We need some way to identify whether a vma is a tree node, head of a vm_set
 * list, or just a member of a vm_set list. We cannot use vm_flags to store
 * such information. The reason is, in the above figure, it is possible that
 * vm_flags' of R and H are covered by the different mmap_sems. When R is
 * removed under R->mmap_sem, H replaces R as a tree node. Since we do not hold
 * H->mmap_sem, we cannot use H->vm_flags for marking that H is a tree node now.
 * That's why some trick involving shared.vm_set.parent is used for identifying
 * tree nodes and list head nodes.
 *
 * vma radix priority search tree node rules:
 *
 * vma->shared.vm_set.parent != NULL    ==> a tree node
 *      vma->shared.vm_set.head != NULL ==> list of others mapping same range
 *      vma->shared.vm_set.head == NULL ==> no others map the same range
 *
 * vma->shared.vm_set.parent == NULL
 * 	vma->shared.vm_set.head != NULL ==> list head of vmas mapping same range
 * 	vma->shared.vm_set.head == NULL ==> a list node
 */

/*
 * Add a new vma known to map the same set of pages as the old vma:
 * useful for fork's dup_mmap as well as vma_prio_tree_insert below.
 * Note that it just happens to work correctly on i_mmap_nonlinear too.
 */
void vma_prio_tree_add(struct vm_area_struct *vma, struct vm_area_struct *old)
{
	/* Leave these BUG_ONs till prio_tree patch stabilizes */
	BUG_ON(RADIX_INDEX(vma) != RADIX_INDEX(old));
	BUG_ON(HEAP_INDEX(vma) != HEAP_INDEX(old));

	vma->shared.vm_set.head = NULL;
	vma->shared.vm_set.parent = NULL;

	if (!old->shared.vm_set.parent)
		list_add(&vma->shared.vm_set.list,
				&old->shared.vm_set.list);
	else if (old->shared.vm_set.head)
		list_add_tail(&vma->shared.vm_set.list,
				&old->shared.vm_set.head->shared.vm_set.list);
	else {
		INIT_LIST_HEAD(&vma->shared.vm_set.list);
		vma->shared.vm_set.head = old;
		old->shared.vm_set.head = vma;
	}
}

void vma_prio_tree_insert(struct vm_area_struct *vma,
			  struct prio_tree_root *root)
{
	struct prio_tree_node *ptr;
	struct vm_area_struct *old;

	vma->shared.vm_set.head = NULL;

	ptr = raw_prio_tree_insert(root, &vma->shared.prio_tree_node);
	if (ptr != (struct prio_tree_node *) &vma->shared.prio_tree_node) {
		old = prio_tree_entry(ptr, struct vm_area_struct,
					shared.prio_tree_node);
		vma_prio_tree_add(vma, old);
	}
}

void vma_prio_tree_remove(struct vm_area_struct *vma,
			  struct prio_tree_root *root)
{
	struct vm_area_struct *node, *head, *new_head;

	if (!vma->shared.vm_set.head) {
		if (!vma->shared.vm_set.parent)
			list_del_init(&vma->shared.vm_set.list);
		else
			raw_prio_tree_remove(root, &vma->shared.prio_tree_node);
	} else {
		/* Leave this BUG_ON till prio_tree patch stabilizes */
		BUG_ON(vma->shared.vm_set.head->shared.vm_set.head != vma);
		if (vma->shared.vm_set.parent) {
			head = vma->shared.vm_set.head;
			if (!list_empty(&head->shared.vm_set.list)) {
				new_head = list_entry(
					head->shared.vm_set.list.next,
					struct vm_area_struct,
					shared.vm_set.list);
				list_del_init(&head->shared.vm_set.list);
			} else
				new_head = NULL;

			raw_prio_tree_replace(root, &vma->shared.prio_tree_node,
					&head->shared.prio_tree_node);
			head->shared.vm_set.head = new_head;
			if (new_head)
				new_head->shared.vm_set.head = head;

		} else {
			node = vma->shared.vm_set.head;
			if (!list_empty(&vma->shared.vm_set.list)) {
				new_head = list_entry(
					vma->shared.vm_set.list.next,
					struct vm_area_struct,
					shared.vm_set.list);
				list_del_init(&vma->shared.vm_set.list);
				node->shared.vm_set.head = new_head;
				new_head->shared.vm_set.head = node;
			} else
				node->shared.vm_set.head = NULL;
		}
	}
}

/*
 * Helper function to enumerate vmas that map a given file page or a set of
 * contiguous file pages. The function returns vmas that at least map a single
 * page in the given range of contiguous file pages.
 */
struct vm_area_struct *vma_prio_tree_next(struct vm_area_struct *vma,
					struct prio_tree_iter *iter)
{
	struct prio_tree_node *ptr;
	struct vm_area_struct *next;

	if (!vma) {
		/*
		 * First call is with NULL vma
		 */
		ptr = prio_tree_next(iter);
		if (ptr) {
			next = prio_tree_entry(ptr, struct vm_area_struct,
						shared.prio_tree_node);
			prefetch(next->shared.vm_set.head);
			return next;
		} else
			return NULL;
	}

	if (vma->shared.vm_set.parent) {
		if (vma->shared.vm_set.head) {
			next = vma->shared.vm_set.head;
			prefetch(next->shared.vm_set.list.next);
			return next;
		}
	} else {
		next = list_entry(vma->shared.vm_set.list.next,
				struct vm_area_struct, shared.vm_set.list);
		if (!next->shared.vm_set.head) {
			prefetch(next->shared.vm_set.list.next);
			return next;
		}
	}

	ptr = prio_tree_next(iter);
	if (ptr) {
		next = prio_tree_entry(ptr, struct vm_area_struct,
					shared.prio_tree_node);
		prefetch(next->shared.vm_set.head);
		return next;
	} else
		return NULL;
}
