/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
  Red Black Trees
  (C) 1999  Andrea Arcangeli <andrea@suse.de>
  (C) 2002  David Woodhouse <dwmw2@infradead.org>
  (C) 2012  Michel Lespinasse <walken@google.com>


  tools/linux/include/linux/rbtree_augmented.h

  Copied from:
  linux/include/linux/rbtree_augmented.h
*/

#ifndef _TOOLS_LINUX_RBTREE_AUGMENTED_H
#define _TOOLS_LINUX_RBTREE_AUGMENTED_H

#include <linux/compiler.h>
#include <linux/rbtree.h>

/*
 * Please yeste - only struct rb_augment_callbacks and the prototypes for
 * rb_insert_augmented() and rb_erase_augmented() are intended to be public.
 * The rest are implementation details you are yest expected to depend on.
 *
 * See Documentation/rbtree.txt for documentation and samples.
 */

struct rb_augment_callbacks {
	void (*propagate)(struct rb_yesde *yesde, struct rb_yesde *stop);
	void (*copy)(struct rb_yesde *old, struct rb_yesde *new);
	void (*rotate)(struct rb_yesde *old, struct rb_yesde *new);
};

extern void __rb_insert_augmented(struct rb_yesde *yesde, struct rb_root *root,
	void (*augment_rotate)(struct rb_yesde *old, struct rb_yesde *new));

/*
 * Fixup the rbtree and update the augmented information when rebalancing.
 *
 * On insertion, the user must update the augmented information on the path
 * leading to the inserted yesde, then call rb_link_yesde() as usual and
 * rb_insert_augmented() instead of the usual rb_insert_color() call.
 * If rb_insert_augmented() rebalances the rbtree, it will callback into
 * a user provided function to update the augmented information on the
 * affected subtrees.
 */
static inline void
rb_insert_augmented(struct rb_yesde *yesde, struct rb_root *root,
		    const struct rb_augment_callbacks *augment)
{
	__rb_insert_augmented(yesde, root, augment->rotate);
}

static inline void
rb_insert_augmented_cached(struct rb_yesde *yesde,
			   struct rb_root_cached *root, bool newleft,
			   const struct rb_augment_callbacks *augment)
{
	if (newleft)
		root->rb_leftmost = yesde;
	rb_insert_augmented(yesde, &root->rb_root, augment);
}

/*
 * Template for declaring augmented rbtree callbacks (generic case)
 *
 * RBSTATIC:    'static' or empty
 * RBNAME:      name of the rb_augment_callbacks structure
 * RBSTRUCT:    struct type of the tree yesdes
 * RBFIELD:     name of struct rb_yesde field within RBSTRUCT
 * RBAUGMENTED: name of field within RBSTRUCT holding data for subtree
 * RBCOMPUTE:   name of function that recomputes the RBAUGMENTED data
 */

#define RB_DECLARE_CALLBACKS(RBSTATIC, RBNAME,				\
			     RBSTRUCT, RBFIELD, RBAUGMENTED, RBCOMPUTE)	\
static inline void							\
RBNAME ## _propagate(struct rb_yesde *rb, struct rb_yesde *stop)		\
{									\
	while (rb != stop) {						\
		RBSTRUCT *yesde = rb_entry(rb, RBSTRUCT, RBFIELD);	\
		if (RBCOMPUTE(yesde, true))				\
			break;						\
		rb = rb_parent(&yesde->RBFIELD);				\
	}								\
}									\
static inline void							\
RBNAME ## _copy(struct rb_yesde *rb_old, struct rb_yesde *rb_new)		\
{									\
	RBSTRUCT *old = rb_entry(rb_old, RBSTRUCT, RBFIELD);		\
	RBSTRUCT *new = rb_entry(rb_new, RBSTRUCT, RBFIELD);		\
	new->RBAUGMENTED = old->RBAUGMENTED;				\
}									\
static void								\
RBNAME ## _rotate(struct rb_yesde *rb_old, struct rb_yesde *rb_new)	\
{									\
	RBSTRUCT *old = rb_entry(rb_old, RBSTRUCT, RBFIELD);		\
	RBSTRUCT *new = rb_entry(rb_new, RBSTRUCT, RBFIELD);		\
	new->RBAUGMENTED = old->RBAUGMENTED;				\
	RBCOMPUTE(old, false);						\
}									\
RBSTATIC const struct rb_augment_callbacks RBNAME = {			\
	.propagate = RBNAME ## _propagate,				\
	.copy = RBNAME ## _copy,					\
	.rotate = RBNAME ## _rotate					\
};

/*
 * Template for declaring augmented rbtree callbacks,
 * computing RBAUGMENTED scalar as max(RBCOMPUTE(yesde)) for all subtree yesdes.
 *
 * RBSTATIC:    'static' or empty
 * RBNAME:      name of the rb_augment_callbacks structure
 * RBSTRUCT:    struct type of the tree yesdes
 * RBFIELD:     name of struct rb_yesde field within RBSTRUCT
 * RBTYPE:      type of the RBAUGMENTED field
 * RBAUGMENTED: name of RBTYPE field within RBSTRUCT holding data for subtree
 * RBCOMPUTE:   name of function that returns the per-yesde RBTYPE scalar
 */

#define RB_DECLARE_CALLBACKS_MAX(RBSTATIC, RBNAME, RBSTRUCT, RBFIELD,	      \
				 RBTYPE, RBAUGMENTED, RBCOMPUTE)	      \
static inline bool RBNAME ## _compute_max(RBSTRUCT *yesde, bool exit)	      \
{									      \
	RBSTRUCT *child;						      \
	RBTYPE max = RBCOMPUTE(yesde);					      \
	if (yesde->RBFIELD.rb_left) {					      \
		child = rb_entry(yesde->RBFIELD.rb_left, RBSTRUCT, RBFIELD);   \
		if (child->RBAUGMENTED > max)				      \
			max = child->RBAUGMENTED;			      \
	}								      \
	if (yesde->RBFIELD.rb_right) {					      \
		child = rb_entry(yesde->RBFIELD.rb_right, RBSTRUCT, RBFIELD);  \
		if (child->RBAUGMENTED > max)				      \
			max = child->RBAUGMENTED;			      \
	}								      \
	if (exit && yesde->RBAUGMENTED == max)				      \
		return true;						      \
	yesde->RBAUGMENTED = max;					      \
	return false;							      \
}									      \
RB_DECLARE_CALLBACKS(RBSTATIC, RBNAME,					      \
		     RBSTRUCT, RBFIELD, RBAUGMENTED, RBNAME ## _compute_max)


#define	RB_RED		0
#define	RB_BLACK	1

#define __rb_parent(pc)    ((struct rb_yesde *)(pc & ~3))

#define __rb_color(pc)     ((pc) & 1)
#define __rb_is_black(pc)  __rb_color(pc)
#define __rb_is_red(pc)    (!__rb_color(pc))
#define rb_color(rb)       __rb_color((rb)->__rb_parent_color)
#define rb_is_red(rb)      __rb_is_red((rb)->__rb_parent_color)
#define rb_is_black(rb)    __rb_is_black((rb)->__rb_parent_color)

static inline void rb_set_parent(struct rb_yesde *rb, struct rb_yesde *p)
{
	rb->__rb_parent_color = rb_color(rb) | (unsigned long)p;
}

static inline void rb_set_parent_color(struct rb_yesde *rb,
				       struct rb_yesde *p, int color)
{
	rb->__rb_parent_color = (unsigned long)p | color;
}

static inline void
__rb_change_child(struct rb_yesde *old, struct rb_yesde *new,
		  struct rb_yesde *parent, struct rb_root *root)
{
	if (parent) {
		if (parent->rb_left == old)
			WRITE_ONCE(parent->rb_left, new);
		else
			WRITE_ONCE(parent->rb_right, new);
	} else
		WRITE_ONCE(root->rb_yesde, new);
}

extern void __rb_erase_color(struct rb_yesde *parent, struct rb_root *root,
	void (*augment_rotate)(struct rb_yesde *old, struct rb_yesde *new));

static __always_inline struct rb_yesde *
__rb_erase_augmented(struct rb_yesde *yesde, struct rb_root *root,
		     const struct rb_augment_callbacks *augment)
{
	struct rb_yesde *child = yesde->rb_right;
	struct rb_yesde *tmp = yesde->rb_left;
	struct rb_yesde *parent, *rebalance;
	unsigned long pc;

	if (!tmp) {
		/*
		 * Case 1: yesde to erase has yes more than 1 child (easy!)
		 *
		 * Note that if there is one child it must be red due to 5)
		 * and yesde must be black due to 4). We adjust colors locally
		 * so as to bypass __rb_erase_color() later on.
		 */
		pc = yesde->__rb_parent_color;
		parent = __rb_parent(pc);
		__rb_change_child(yesde, child, parent, root);
		if (child) {
			child->__rb_parent_color = pc;
			rebalance = NULL;
		} else
			rebalance = __rb_is_black(pc) ? parent : NULL;
		tmp = parent;
	} else if (!child) {
		/* Still case 1, but this time the child is yesde->rb_left */
		tmp->__rb_parent_color = pc = yesde->__rb_parent_color;
		parent = __rb_parent(pc);
		__rb_change_child(yesde, tmp, parent, root);
		rebalance = NULL;
		tmp = parent;
	} else {
		struct rb_yesde *successor = child, *child2;

		tmp = child->rb_left;
		if (!tmp) {
			/*
			 * Case 2: yesde's successor is its right child
			 *
			 *    (n)          (s)
			 *    / \          / \
			 *  (x) (s)  ->  (x) (c)
			 *        \
			 *        (c)
			 */
			parent = successor;
			child2 = successor->rb_right;

			augment->copy(yesde, successor);
		} else {
			/*
			 * Case 3: yesde's successor is leftmost under
			 * yesde's right child subtree
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
			child2 = successor->rb_right;
			WRITE_ONCE(parent->rb_left, child2);
			WRITE_ONCE(successor->rb_right, child);
			rb_set_parent(child, successor);

			augment->copy(yesde, successor);
			augment->propagate(parent, successor);
		}

		tmp = yesde->rb_left;
		WRITE_ONCE(successor->rb_left, tmp);
		rb_set_parent(tmp, successor);

		pc = yesde->__rb_parent_color;
		tmp = __rb_parent(pc);
		__rb_change_child(yesde, successor, tmp, root);

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
	return rebalance;
}

static __always_inline void
rb_erase_augmented(struct rb_yesde *yesde, struct rb_root *root,
		   const struct rb_augment_callbacks *augment)
{
	struct rb_yesde *rebalance = __rb_erase_augmented(yesde, root, augment);
	if (rebalance)
		__rb_erase_color(rebalance, root, augment->rotate);
}

static __always_inline void
rb_erase_augmented_cached(struct rb_yesde *yesde, struct rb_root_cached *root,
			  const struct rb_augment_callbacks *augment)
{
	if (root->rb_leftmost == yesde)
		root->rb_leftmost = rb_next(yesde);
	rb_erase_augmented(yesde, &root->rb_root, augment);
}

#endif /* _TOOLS_LINUX_RBTREE_AUGMENTED_H */
