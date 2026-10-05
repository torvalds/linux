/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
  Red Black Trees
  (C) 1999  Andrea Arcangeli <andrea@suse.de>
  (C) 2002  David Woodhouse <dwmw2@infradead.org>
  (C) 2012  Michel Lespinasse <walken@google.com>


  linux/include/linux/rbtree_augmented.h
*/

#ifndef _LINUX_RBTREE_AUGMENTED_H
#define _LINUX_RBTREE_AUGMENTED_H

#include <linux/compiler.h>
#include <linux/rbtree.h>
#include <linux/rcupdate.h>
#include <linux/args.h>
#include <linux/minmax.h>

/*
 * Please note - only struct rb_augment_callbacks and the prototypes for
 * rb_insert_augmented() and rb_erase_augmented() are intended to be public.
 * The rest are implementation details you are not expected to depend on.
 *
 * See Documentation/core-api/rbtree.rst for documentation and samples.
 */

struct rb_augment_callbacks {
	void (*propagate)(struct rb_node *node, struct rb_node *stop);
	void (*copy)(struct rb_node *old, struct rb_node *new);
	void (*rotate)(struct rb_node *old, struct rb_node *new);
};

extern void __rb_insert_augmented(struct rb_node *node, struct rb_root *root,
	void (*augment_rotate)(struct rb_node *old, struct rb_node *new));

/*
 * Fixup the rbtree and update the augmented information when rebalancing.
 *
 * On insertion, the user must update the augmented information on the path
 * leading to the inserted node, then call rb_link_node() as usual and
 * rb_insert_augmented() instead of the usual rb_insert_color() call.
 * If rb_insert_augmented() rebalances the rbtree, it will callback into
 * a user provided function to update the augmented information on the
 * affected subtrees.
 */
static inline void
rb_insert_augmented(struct rb_node *node, struct rb_root *root,
		    const struct rb_augment_callbacks *augment)
{
	__rb_insert_augmented(node, root, augment->rotate);
}

static inline void
rb_insert_augmented_cached(struct rb_node *node,
			   struct rb_root_cached *root, bool newleft,
			   const struct rb_augment_callbacks *augment)
{
	if (newleft)
		root->rb_leftmost = node;
	rb_insert_augmented(node, &root->rb_root, augment);
}

static __always_inline struct rb_node *
rb_add_augmented_cached(struct rb_node *node, struct rb_root_cached *tree,
			bool (*less)(struct rb_node *, const struct rb_node *),
			const struct rb_augment_callbacks *augment)
{
	struct rb_node **link = &tree->rb_root.rb_node;
	struct rb_node *parent = NULL;
	bool leftmost = true;

	while (*link) {
		parent = *link;
		if (less(node, parent)) {
			link = &parent->rb_left;
		} else {
			link = &parent->rb_right;
			leftmost = false;
		}
	}

	rb_link_node(node, parent, link);
	augment->propagate(parent, NULL); /* suboptimal */
	rb_insert_augmented_cached(node, tree, leftmost, augment);

	return leftmost ? node : NULL;
}

#define RB_FOR_EACH_1(what, RBNAME, RBSTRUCT, RBFIELD, x)		\
	what(1, RBNAME, RBSTRUCT, RBFIELD, x)

#define RB_FOR_EACH_2(what, RBNAME, RBSTRUCT, RBFIELD, x, ...)		\
	what(2, RBNAME, RBSTRUCT, RBFIELD, x)				\
	RB_FOR_EACH_1(what, RBNAME, RBSTRUCT, RBFIELD, __VA_ARGS__)

#define RB_FOR_EACH_3(what, RBNAME, RBSTRUCT, RBFIELD, x, ...)		\
	what(3, RBNAME, RBSTRUCT, RBFIELD, x)				\
	RB_FOR_EACH_2(what, RBNAME, RBSTRUCT, RBFIELD, __VA_ARGS__)

#define RB_FOR_EACH_4(what, RBNAME, RBSTRUCT, RBFIELD, x, ...)		\
	what(4, RBNAME, RBSTRUCT, RBFIELD, x)				\
	RB_FOR_EACH_3(what, RBNAME, RBSTRUCT, RBFIELD, __VA_ARGS__)

#define RB_FOR_EACH_5(what, RBNAME, RBSTRUCT, RBFIELD, x, ...)		\
	what(5, RBNAME, RBSTRUCT, RBFIELD, x)				\
	RB_FOR_EACH_4(what, RBNAME, RBSTRUCT, RBFIELD, __VA_ARGS__)

#define RB_FOR_EACH_6(what, RBNAME, RBSTRUCT, RBFIELD, x, ...)		\
	what(6, RBNAME, RBSTRUCT, RBFIELD, x)				\
	RB_FOR_EACH_5(what, RBNAME, RBSTRUCT, RBFIELD, __VA_ARGS__)

#define RB_FOR_EACH_7(what, RBNAME, RBSTRUCT, RBFIELD, x, ...)		\
	what(7, RBNAME, RBSTRUCT, RBFIELD, x)				\
	RB_FOR_EACH_6(what, RBNAME, RBSTRUCT, RBFIELD, __VA_ARGS__)

#define RB_FOR_EACH_8(what, RBNAME, RBSTRUCT, RBFIELD, x, ...)		\
	what(8, RBNAME, RBSTRUCT, RBFIELD, x)				\
	RB_FOR_EACH_7(what, RBNAME, RBSTRUCT, RBFIELD, __VA_ARGS__)

#define RB_FOR_EACH(action, RBNAME, RBSTRUCT, RBFIELD, ...)		\
	CONCATENATE(RB_FOR_EACH_, COUNT_ARGS(__VA_ARGS__))		\
		(action, RBNAME, RBSTRUCT, RBFIELD, __VA_ARGS__)

/*
 * One augmented field: @val is the node's own contribution (a member for
 * RB_AUG(), a function of the node for RB_AUG_FUNC()), @aug the member
 * holding the aggregate over the subtree, and @fold(a, b) combines two
 * aggregates: min, max, a sum, ...  It must be commutative and associative.
 */
#define RB_AUG_FUNC(val, aug, fold) (val(s), aug, fold)
#define RB_AUG(val, aug, fold) (s->val, aug, fold)
#define RB_UNPACK(...) __VA_ARGS__

#define __RB_INST(n, RBNAME, RBSTRUCT, RBFIELD, val, aug, fold)		\
static inline void							\
RBNAME ## _copy_ ## n(RBSTRUCT *old, RBSTRUCT *new)			\
{									\
	new->aug = old->aug;						\
}									\
static inline bool							\
RBNAME ## _compute_ ## n(RBSTRUCT *s, bool exit)			\
{									\
	TYPEOF_UNQUAL(s->aug) _old_aug = s->aug;			\
	TYPEOF_UNQUAL(s->aug) _val = val;				\
	struct rb_node *_node = &s->RBFIELD;				\
	if (_node->rb_right) {						\
		RBSTRUCT *_c = container_of(_node->rb_right, typeof(*s), RBFIELD); \
		_val = fold(_val, _c->aug);				\
	}								\
	if (_node->rb_left) {						\
		RBSTRUCT *_c = container_of(_node->rb_left, typeof(*s), RBFIELD); \
		_val = fold(_val, _c->aug);				\
	}								\
	if (exit && _old_aug == _val)					\
		return true;						\
	s->aug = _val;							\
	return false;							\
}
#define _RB_INST(n, RBNAME, RBSTRUCT, RBFIELD, args)			\
	__RB_INST(n, RBNAME, RBSTRUCT, RBFIELD, args)
#define RB_INST(n, RBNAME, RBSTRUCT, RBFIELD, x)			\
	_RB_INST(n, RBNAME, RBSTRUCT, RBFIELD, RB_UNPACK x)

#define __RB_COPY(n, RBNAME, RBSTRUCT, RBFIELD, val, aug, fold)		\
	RBNAME ## _copy_ ## n(old, new);
#define _RB_COPY(n, RBNAME, RBSTRUCT, RBFIELD, args)			\
	__RB_COPY(n, RBNAME, RBSTRUCT, RBFIELD, args)
#define RB_COPY(n, RBNAME, RBSTRUCT, RBFIELD, x)			\
	_RB_COPY(n, RBNAME, RBSTRUCT, RBFIELD, RB_UNPACK x)

#define __RB_COMPUTE(n, RBNAME, RBSTRUCT, RBFIELD, val, aug, fold)	\
	ret &= RBNAME ## _compute_ ## n(node, exit);
#define _RB_COMPUTE(n, RBNAME, RBSTRUCT, RBFIELD, args)			\
	__RB_COMPUTE(n, RBNAME, RBSTRUCT, RBFIELD, args)
#define RB_COMPUTE(n, RBNAME, RBSTRUCT, RBFIELD, x)			\
	_RB_COMPUTE(n, RBNAME, RBSTRUCT, RBFIELD, RB_UNPACK x)

/*
 * Template for declaring augmented rbtree callbacks (generic multi fields)
 *
 * RBSTATIC:    'static' or empty
 * RBNAME:      name of the rb_augment_callbacks structure
 * RBSTRUCT:    struct type of the tree nodes
 * RBFIELD:     name of struct rb_node field within RBSTRUCT
 * RBAUG...:	list of RB_AUG() describing the augmented data
 */
#define RB_DECLARE_CALLBACKS(RBSTATIC, RBNAME,				\
			     RBSTRUCT, RBFIELD, RBAUG...)		\
RB_FOR_EACH(RB_INST, RBNAME, RBSTRUCT, RBFIELD, RBAUG)			\
static inline void							\
RBNAME ## __copy(RBSTRUCT *old, RBSTRUCT *new)				\
{									\
	RB_FOR_EACH(RB_COPY, RBNAME, RBSTRUCT, RBFIELD, RBAUG);		\
}									\
static inline bool							\
RBNAME ## __compute(RBSTRUCT *node, bool exit)				\
{									\
	bool ret = true;						\
	RB_FOR_EACH(RB_COMPUTE, RBNAME, RBSTRUCT, RBFIELD, RBAUG);	\
	return ret;							\
}									\
static inline void							\
RBNAME ## _propagate(struct rb_node *rb, struct rb_node *stop)		\
{									\
	while (rb != stop) {						\
		RBSTRUCT *node = rb_entry(rb, RBSTRUCT, RBFIELD);	\
		if (RBNAME ## __compute(node, true))			\
			break;						\
		rb = rb_parent(&node->RBFIELD);				\
	}								\
}									\
static inline void							\
RBNAME ## _copy(struct rb_node *rb_old, struct rb_node *rb_new)		\
{									\
	RBSTRUCT *old = rb_entry(rb_old, RBSTRUCT, RBFIELD);		\
	RBSTRUCT *new = rb_entry(rb_new, RBSTRUCT, RBFIELD);		\
	RBNAME ## __copy(old, new);					\
}									\
static void								\
RBNAME ## _rotate(struct rb_node *rb_old, struct rb_node *rb_new)	\
{									\
	RBSTRUCT *old = rb_entry(rb_old, RBSTRUCT, RBFIELD);		\
	RBSTRUCT *new = rb_entry(rb_new, RBSTRUCT, RBFIELD);		\
	RBNAME ## __copy(old, new);					\
	RBNAME ## __compute(old, false);				\
}									\
RBSTATIC const struct rb_augment_callbacks RBNAME = {			\
	.propagate = RBNAME ## _propagate,				\
	.copy = RBNAME ## _copy,					\
	.rotate = RBNAME ## _rotate					\
};

/*
 * Template for declaring augmented rbtree callbacks,
 * computing RBAUGMENTED scalar as max(RBCOMPUTE(node)) for all subtree nodes.
 *
 * RBSTATIC:    'static' or empty
 * RBNAME:      name of the rb_augment_callbacks structure
 * RBSTRUCT:    struct type of the tree nodes
 * RBFIELD:     name of struct rb_node field within RBSTRUCT
 * RBTYPE:      type of the RBAUGMENTED field -- unused, assumed typeof(RBAUGMENTED)
 * RBAUGMENTED: name of field within RBSTRUCT holding data for subtree
 * RBVALUE:     name of function that returns the per-node RBTYPE scalar
 */

#define RB_DECLARE_CALLBACKS_MAX(RBSTATIC, RBNAME, RBSTRUCT, RBFIELD,		\
				 RBTYPE, RBAUGMENTED, RBVALUE)			\
RB_DECLARE_CALLBACKS(RBSTATIC, RBNAME, RBSTRUCT, RBFIELD,			\
		     RB_AUG_FUNC(RBVALUE, RBAUGMENTED, max))


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
	rb->__rb_parent_color = rb_color(rb) + (unsigned long)p;
}

static inline void rb_set_parent_color(struct rb_node *rb,
				       struct rb_node *p, int color)
{
	rb->__rb_parent_color = (unsigned long)p + color;
}

static inline void
__rb_change_child(struct rb_node *old, struct rb_node *new,
		  struct rb_node *parent, struct rb_root *root)
{
	if (parent) {
		if (parent->rb_left == old)
			WRITE_ONCE(parent->rb_left, new);
		else
			WRITE_ONCE(parent->rb_right, new);
	} else
		WRITE_ONCE(root->rb_node, new);
}

static inline void
__rb_change_child_rcu(struct rb_node *old, struct rb_node *new,
		      struct rb_node *parent, struct rb_root *root)
{
	if (parent) {
		if (parent->rb_left == old)
			rcu_assign_pointer(parent->rb_left, new);
		else
			rcu_assign_pointer(parent->rb_right, new);
	} else
		rcu_assign_pointer(root->rb_node, new);
}

extern void __rb_erase_color(struct rb_node *parent, struct rb_root *root,
	void (*augment_rotate)(struct rb_node *old, struct rb_node *new));

static __always_inline struct rb_node *
__rb_erase_augmented(struct rb_node *node, struct rb_root *root,
		     const struct rb_augment_callbacks *augment)
{
	struct rb_node *child = node->rb_right;
	struct rb_node *tmp = node->rb_left;
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
			child2 = successor->rb_right;
			WRITE_ONCE(parent->rb_left, child2);
			WRITE_ONCE(successor->rb_right, child);
			rb_set_parent(child, successor);

			augment->copy(node, successor);
			augment->propagate(parent, successor);
		}

		tmp = node->rb_left;
		WRITE_ONCE(successor->rb_left, tmp);
		rb_set_parent(tmp, successor);

		pc = node->__rb_parent_color;
		tmp = __rb_parent(pc);
		__rb_change_child(node, successor, tmp, root);

		if (child2) {
			rb_set_parent_color(child2, parent, RB_BLACK);
			rebalance = NULL;
		} else {
			rebalance = rb_is_black(successor) ? parent : NULL;
		}
		successor->__rb_parent_color = pc;
		tmp = successor;
	}

	augment->propagate(tmp, NULL);
	return rebalance;
}

static __always_inline void
rb_erase_augmented(struct rb_node *node, struct rb_root *root,
		   const struct rb_augment_callbacks *augment)
{
	struct rb_node *rebalance = __rb_erase_augmented(node, root, augment);
	if (rebalance)
		__rb_erase_color(rebalance, root, augment->rotate);
}

static __always_inline void
rb_erase_augmented_cached(struct rb_node *node, struct rb_root_cached *root,
			  const struct rb_augment_callbacks *augment)
{
	if (root->rb_leftmost == node)
		root->rb_leftmost = rb_next(node);
	rb_erase_augmented(node, &root->rb_root, augment);
}

#endif	/* _LINUX_RBTREE_AUGMENTED_H */
