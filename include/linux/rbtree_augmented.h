/*
  Red Black Trees
  (C) 1999  Andrea Arcangeli <andrea@suse.de>
  (C) 2002  David Woodhouse <dwmw2@infradead.org>
  (C) 2012  Michel Lespinasse <walken@google.com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

  linux/include/linux/rbtree_augmented.h
*/

#ifndef _LINUX_RBTREE_AUGMENTED_H
#define _LINUX_RBTREE_AUGMENTED_H

#include <linux/compiler.h>
#include <linux/rbtree.h>

/*
 * Please note - only struct rb_augment_callbacks and the prototypes for
 * rb_insert_augmented() and rb_erase_augmented() are intended to be public.
 * The rest are implementation details you are not expected to depend on.
 *
 * See Documentation/rbtree.txt for documentation and samples.
 */

struct rb_augment_callbacks {
	void (*propagate)(struct rb_node *node, struct rb_node *stop);
	void (*copy)(struct rb_node *old, struct rb_node *new);
	void (*rotate)(struct rb_node *old, struct rb_node *new);
};

extern void __rb_insert_augmented(struct rb_node *node, struct rb_root *root,
	void (*augment_rotate)(struct rb_node *old, struct rb_node *new));
static inline void
rb_insert_augmented(struct rb_node *node, struct rb_root *root,
		    const struct rb_augment_callbacks *augment)
{
	__rb_insert_augmented(node, root, augment->rotate);
}

#define RB_DECLARE_CALLBACKS(rbstatic, rbname, rbstruct, rbfield,	\
			     rbtype, rbaugmented, rbcompute)		\
static inline void							\
rbname ## _propagate(struct rb_node *rb, struct rb_node *stop)		\
{									\
	while (rb != stop) {						\
		rbstruct *node = rb_entry(rb, rbstruct, rbfield);	\
		rbtype augmented = rbcompute(node);			\
		if (node->rbaugmented == augmented)			\
			break;						\
		node->rbaugmented = augmented;				\
		rb = rb_parent(&node->rbfield);				\
	}								\
}									\
static inline void							\
rbname ## _copy(struct rb_node *rb_old, struct rb_node *rb_new)		\
{									\
	rbstruct *old = rb_entry(rb_old, rbstruct, rbfield);		\
	rbstruct *new = rb_entry(rb_new, rbstruct, rbfield);		\
	new->rbaugmented = old->rbaugmented;				\
}									\
static void								\
rbname ## _rotate(struct rb_node *rb_old, struct rb_node *rb_new)	\
{									\
	rbstruct *old = rb_entry(rb_old, rbstruct, rbfield);		\
	rbstruct *new = rb_entry(rb_new, rbstruct, rbfield);		\
	new->rbaugmented = old->rbaugmented;				\
	old->rbaugmented = rbcompute(old);				\
}									\
rbstatic const struct rb_augment_callbacks rbname = {			\
	rbname ## _propagate, rbname ## _copy, rbname ## _rotate	\
};


#define	RB_RED		0
#define	RB_BLACK	1

#define __rb_parent(pc)    ((struct rb_node *)(pc & ~3))

#define __rb_color(pc)     ((pc) & 1)
#define __rb_is_black(pc)  __rb_color(pc)
#define __rb_is_red(pc)    (!__rb_color(pc))
#define rb_color(rb)       __rb_color((rb)->__rb_parent_color)
#define rb_is_red(rb)      __rb_is_red((rb)->__rb_parent_color)
#define rb_is_black(rb)    __rb_is_black((rb)->__rb_parent_color)

static inline void rb_set_parent(struct rb_node *rb, struct rb_node *p)
{
	rb->__rb_parent_color = rb_color(rb) | (unsigned long)p;
}

static inline void rb_set_parent_color(struct rb_node *rb,
				       struct rb_node *p, int color)
{
	rb->__rb_parent_color = (unsigned long)p | color;
}

static inline void
__rb_change_child(struct rb_node *old, struct rb_node *new,
		  struct rb_node *parent, struct rb_root *root)
{
	if (parent) {
		if (parent->rb_left == old)
			parent->rb_left = new;
		else
			parent->rb_right = new;
	} else
		root->rb_node = new;
}

extern void __rb_erase_color(struct rb_node *parent, struct rb_root *root,
	void (*augment_rotate)(struct rb_node *old, struct rb_node *new));

static __always_inline void
rb_erase_augmented(struct rb_node *node, struct rb_root *root,
		   const struct rb_augment_callbacks *augment)
{
	struct rb_node *child = node->rb_right, *tmp = node->rb_left;
	struct rb_node *parent, *rebalance;
	unsigned long pc;

	if (!tmp) {
		/*
		 * Case 1: node to erase has no more than 1 child (easy!)
		 *
		 * Note that if there is one child it must be red due to 5)
		 * and node must be black due to 4). We adjust colors locally
		 * so as to bypass __rb_erase_color() later on.
		 */
		pc = node->__rb_parent_color;
		parent = __rb_parent(pc);
		__rb_change_child(node, child, parent, root);
		if (child) {
			child->__rb_parent_color = pc;
			rebalance = NULL;
		} else
			rebalance = __rb_is_black(pc) ? parent : NULL;
		tmp = parent;
	} else if (!child) {
		/* Still case 1, but this time the child is node->rb_left */
		tmp->__rb_parent_color = pc = node->__rb_parent_color;
		parent = __rb_parent(pc);
		__rb_change_child(node, tmp, parent, root);
		rebalance = NULL;
		tmp = parent;
	} else {
		struct rb_node *successor = child, *child2;
		tmp = child->rb_left;
		if (!tmp) {
			/*
			 * Case 2: node's successor is its right child
			 *
			 *    (n)          (s)
			 *    / \          / \
			 *  (x) (s)  ->  (x) (c)
			 *        \
			 *        (c)
			 */
			parent = successor;
			child2 = successor->rb_right;
			augment->copy(node, successor);
		} else {
			/*
			 * Case 3: node's successor is leftmost under
			 * node's right child subtree
			 *
			 *    (n)          (s)
			 *    / \          / \
			 *  (x) (y)  ->  (x) (y)
			 *      /            /
			 *    (p)          (p)
			 *    /            /
			 *  (s)          (c)
			 *    \
			 *    (c)
			 */
			do {
				parent = successor;
				successor = tmp;
				tmp = tmp->rb_left;
			} while (tmp);
			parent->rb_left = child2 = successor->rb_right;
			successor->rb_right = child;
			rb_set_parent(child, successor);
			augment->copy(node, successor);
			augment->propagate(parent, successor);
		}

		successor->rb_left = tmp = node->rb_left;
		rb_set_parent(tmp, successor);

		pc = node->__rb_parent_color;
		tmp = __rb_parent(pc);
		__rb_change_child(node, successor, tmp, root);
		if (child2) {
			successor->__rb_parent_color = pc;
			rb_set_parent_color(child2, parent, RB_BLACK);
			rebalance = NULL;
		} else {
			unsigned long pc2 = successor->__rb_parent_color;
			successor->__rb_parent_color = pc;
			rebalance = __rb_is_black(pc2) ? parent : NULL;
		}
		tmp = successor;
	}

	augment->propagate(tmp, NULL);
	if (rebalance)
		__rb_erase_color(rebalance, root, augment->rotate);
}

#endif	/* _LINUX_RBTREE_AUGMENTED_H */
