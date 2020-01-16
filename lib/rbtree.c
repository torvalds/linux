// SPDX-License-Identifier: GPL-2.0-or-later
/*
  Red Black Trees
  (C) 1999  Andrea Arcangeli <andrea@suse.de>
  (C) 2002  David Woodhouse <dwmw2@infradead.org>
  (C) 2012  Michel Lespinasse <walken@google.com>


  linux/lib/rbtree.c
*/

#include <linux/rbtree_augmented.h>
#include <linux/export.h>

/*
 * red-black trees properties:  http://en.wikipedia.org/wiki/Rbtree
 *
 *  1) A yesde is either red or black
 *  2) The root is black
 *  3) All leaves (NULL) are black
 *  4) Both children of every red yesde are black
 *  5) Every simple path from root to leaves contains the same number
 *     of black yesdes.
 *
 *  4 and 5 give the O(log n) guarantee, since 4 implies you canyest have two
 *  consecutive red yesdes in a path and every red yesde is therefore followed by
 *  a black. So if B is the number of black yesdes on every simple path (as per
 *  5), then the longest possible path due to 4 is 2B.
 *
 *  We shall indicate color with case, where black yesdes are uppercase and red
 *  yesdes will be lowercase. Unkyeswn color yesdes shall be drawn as red within
 *  parentheses and have some accompanying text comment.
 */

/*
 * Notes on lockless lookups:
 *
 * All stores to the tree structure (rb_left and rb_right) must be done using
 * WRITE_ONCE(). And we must yest inadvertently cause (temporary) loops in the
 * tree structure as seen in program order.
 *
 * These two requirements will allow lockless iteration of the tree -- yest
 * correct iteration mind you, tree rotations are yest atomic so a lookup might
 * miss entire subtrees.
 *
 * But they do guarantee that any such traversal will only see valid elements
 * and that it will indeed complete -- does yest get stuck in a loop.
 *
 * It also guarantees that if the lookup returns an element it is the 'correct'
 * one. But yest returning an element does _NOT_ mean it's yest present.
 *
 * NOTE:
 *
 * Stores to __rb_parent_color are yest important for simple lookups so those
 * are left undone as of yesw. Nor did I check for loops involving parent
 * pointers.
 */

static inline void rb_set_black(struct rb_yesde *rb)
{
	rb->__rb_parent_color |= RB_BLACK;
}

static inline struct rb_yesde *rb_red_parent(struct rb_yesde *red)
{
	return (struct rb_yesde *)red->__rb_parent_color;
}

/*
 * Helper function for rotations:
 * - old's parent and color get assigned to new
 * - old gets assigned new as a parent and 'color' as a color.
 */
static inline void
__rb_rotate_set_parents(struct rb_yesde *old, struct rb_yesde *new,
			struct rb_root *root, int color)
{
	struct rb_yesde *parent = rb_parent(old);
	new->__rb_parent_color = old->__rb_parent_color;
	rb_set_parent_color(old, new, color);
	__rb_change_child(old, new, parent, root);
}

static __always_inline void
__rb_insert(struct rb_yesde *yesde, struct rb_root *root,
	    void (*augment_rotate)(struct rb_yesde *old, struct rb_yesde *new))
{
	struct rb_yesde *parent = rb_red_parent(yesde), *gparent, *tmp;

	while (true) {
		/*
		 * Loop invariant: yesde is red.
		 */
		if (unlikely(!parent)) {
			/*
			 * The inserted yesde is root. Either this is the
			 * first yesde, or we recursed at Case 1 below and
			 * are yes longer violating 4).
			 */
			rb_set_parent_color(yesde, NULL, RB_BLACK);
			break;
		}

		/*
		 * If there is a black parent, we are done.
		 * Otherwise, take some corrective action as,
		 * per 4), we don't want a red root or two
		 * consecutive red yesdes.
		 */
		if(rb_is_black(parent))
			break;

		gparent = rb_red_parent(parent);

		tmp = gparent->rb_right;
		if (parent != tmp) {	/* parent == gparent->rb_left */
			if (tmp && rb_is_red(tmp)) {
				/*
				 * Case 1 - yesde's uncle is red (color flips).
				 *
				 *       G            g
				 *      / \          / \
				 *     p   u  -->   P   U
				 *    /            /
				 *   n            n
				 *
				 * However, since g's parent might be red, and
				 * 4) does yest allow this, we need to recurse
				 * at g.
				 */
				rb_set_parent_color(tmp, gparent, RB_BLACK);
				rb_set_parent_color(parent, gparent, RB_BLACK);
				yesde = gparent;
				parent = rb_parent(yesde);
				rb_set_parent_color(yesde, parent, RB_RED);
				continue;
			}

			tmp = parent->rb_right;
			if (yesde == tmp) {
				/*
				 * Case 2 - yesde's uncle is black and yesde is
				 * the parent's right child (left rotate at parent).
				 *
				 *      G             G
				 *     / \           / \
				 *    p   U  -->    n   U
				 *     \           /
				 *      n         p
				 *
				 * This still leaves us in violation of 4), the
				 * continuation into Case 3 will fix that.
				 */
				tmp = yesde->rb_left;
				WRITE_ONCE(parent->rb_right, tmp);
				WRITE_ONCE(yesde->rb_left, parent);
				if (tmp)
					rb_set_parent_color(tmp, parent,
							    RB_BLACK);
				rb_set_parent_color(parent, yesde, RB_RED);
				augment_rotate(parent, yesde);
				parent = yesde;
				tmp = yesde->rb_right;
			}

			/*
			 * Case 3 - yesde's uncle is black and yesde is
			 * the parent's left child (right rotate at gparent).
			 *
			 *        G           P
			 *       / \         / \
			 *      p   U  -->  n   g
			 *     /                 \
			 *    n                   U
			 */
			WRITE_ONCE(gparent->rb_left, tmp); /* == parent->rb_right */
			WRITE_ONCE(parent->rb_right, gparent);
			if (tmp)
				rb_set_parent_color(tmp, gparent, RB_BLACK);
			__rb_rotate_set_parents(gparent, parent, root, RB_RED);
			augment_rotate(gparent, parent);
			break;
		} else {
			tmp = gparent->rb_left;
			if (tmp && rb_is_red(tmp)) {
				/* Case 1 - color flips */
				rb_set_parent_color(tmp, gparent, RB_BLACK);
				rb_set_parent_color(parent, gparent, RB_BLACK);
				yesde = gparent;
				parent = rb_parent(yesde);
				rb_set_parent_color(yesde, parent, RB_RED);
				continue;
			}

			tmp = parent->rb_left;
			if (yesde == tmp) {
				/* Case 2 - right rotate at parent */
				tmp = yesde->rb_right;
				WRITE_ONCE(parent->rb_left, tmp);
				WRITE_ONCE(yesde->rb_right, parent);
				if (tmp)
					rb_set_parent_color(tmp, parent,
							    RB_BLACK);
				rb_set_parent_color(parent, yesde, RB_RED);
				augment_rotate(parent, yesde);
				parent = yesde;
				tmp = yesde->rb_left;
			}

			/* Case 3 - left rotate at gparent */
			WRITE_ONCE(gparent->rb_right, tmp); /* == parent->rb_left */
			WRITE_ONCE(parent->rb_left, gparent);
			if (tmp)
				rb_set_parent_color(tmp, gparent, RB_BLACK);
			__rb_rotate_set_parents(gparent, parent, root, RB_RED);
			augment_rotate(gparent, parent);
			break;
		}
	}
}

/*
 * Inline version for rb_erase() use - we want to be able to inline
 * and eliminate the dummy_rotate callback there
 */
static __always_inline void
____rb_erase_color(struct rb_yesde *parent, struct rb_root *root,
	void (*augment_rotate)(struct rb_yesde *old, struct rb_yesde *new))
{
	struct rb_yesde *yesde = NULL, *sibling, *tmp1, *tmp2;

	while (true) {
		/*
		 * Loop invariants:
		 * - yesde is black (or NULL on first iteration)
		 * - yesde is yest the root (parent is yest NULL)
		 * - All leaf paths going through parent and yesde have a
		 *   black yesde count that is 1 lower than other leaf paths.
		 */
		sibling = parent->rb_right;
		if (yesde != sibling) {	/* yesde == parent->rb_left */
			if (rb_is_red(sibling)) {
				/*
				 * Case 1 - left rotate at parent
				 *
				 *     P               S
				 *    / \             / \
				 *   N   s    -->    p   Sr
				 *      / \         / \
				 *     Sl  Sr      N   Sl
				 */
				tmp1 = sibling->rb_left;
				WRITE_ONCE(parent->rb_right, tmp1);
				WRITE_ONCE(sibling->rb_left, parent);
				rb_set_parent_color(tmp1, parent, RB_BLACK);
				__rb_rotate_set_parents(parent, sibling, root,
							RB_RED);
				augment_rotate(parent, sibling);
				sibling = tmp1;
			}
			tmp1 = sibling->rb_right;
			if (!tmp1 || rb_is_black(tmp1)) {
				tmp2 = sibling->rb_left;
				if (!tmp2 || rb_is_black(tmp2)) {
					/*
					 * Case 2 - sibling color flip
					 * (p could be either color here)
					 *
					 *    (p)           (p)
					 *    / \           / \
					 *   N   S    -->  N   s
					 *      / \           / \
					 *     Sl  Sr        Sl  Sr
					 *
					 * This leaves us violating 5) which
					 * can be fixed by flipping p to black
					 * if it was red, or by recursing at p.
					 * p is red when coming from Case 1.
					 */
					rb_set_parent_color(sibling, parent,
							    RB_RED);
					if (rb_is_red(parent))
						rb_set_black(parent);
					else {
						yesde = parent;
						parent = rb_parent(yesde);
						if (parent)
							continue;
					}
					break;
				}
				/*
				 * Case 3 - right rotate at sibling
				 * (p could be either color here)
				 *
				 *   (p)           (p)
				 *   / \           / \
				 *  N   S    -->  N   sl
				 *     / \             \
				 *    sl  Sr            S
				 *                       \
				 *                        Sr
				 *
				 * Note: p might be red, and then both
				 * p and sl are red after rotation(which
				 * breaks property 4). This is fixed in
				 * Case 4 (in __rb_rotate_set_parents()
				 *         which set sl the color of p
				 *         and set p RB_BLACK)
				 *
				 *   (p)            (sl)
				 *   / \            /  \
				 *  N   sl   -->   P    S
				 *       \        /      \
				 *        S      N        Sr
				 *         \
				 *          Sr
				 */
				tmp1 = tmp2->rb_right;
				WRITE_ONCE(sibling->rb_left, tmp1);
				WRITE_ONCE(tmp2->rb_right, sibling);
				WRITE_ONCE(parent->rb_right, tmp2);
				if (tmp1)
					rb_set_parent_color(tmp1, sibling,
							    RB_BLACK);
				augment_rotate(sibling, tmp2);
				tmp1 = sibling;
				sibling = tmp2;
			}
			/*
			 * Case 4 - left rotate at parent + color flips
			 * (p and sl could be either color here.
			 *  After rotation, p becomes black, s acquires
			 *  p's color, and sl keeps its color)
			 *
			 *      (p)             (s)
			 *      / \             / \
			 *     N   S     -->   P   Sr
			 *        / \         / \
			 *      (sl) sr      N  (sl)
			 */
			tmp2 = sibling->rb_left;
			WRITE_ONCE(parent->rb_right, tmp2);
			WRITE_ONCE(sibling->rb_left, parent);
			rb_set_parent_color(tmp1, sibling, RB_BLACK);
			if (tmp2)
				rb_set_parent(tmp2, parent);
			__rb_rotate_set_parents(parent, sibling, root,
						RB_BLACK);
			augment_rotate(parent, sibling);
			break;
		} else {
			sibling = parent->rb_left;
			if (rb_is_red(sibling)) {
				/* Case 1 - right rotate at parent */
				tmp1 = sibling->rb_right;
				WRITE_ONCE(parent->rb_left, tmp1);
				WRITE_ONCE(sibling->rb_right, parent);
				rb_set_parent_color(tmp1, parent, RB_BLACK);
				__rb_rotate_set_parents(parent, sibling, root,
							RB_RED);
				augment_rotate(parent, sibling);
				sibling = tmp1;
			}
			tmp1 = sibling->rb_left;
			if (!tmp1 || rb_is_black(tmp1)) {
				tmp2 = sibling->rb_right;
				if (!tmp2 || rb_is_black(tmp2)) {
					/* Case 2 - sibling color flip */
					rb_set_parent_color(sibling, parent,
							    RB_RED);
					if (rb_is_red(parent))
						rb_set_black(parent);
					else {
						yesde = parent;
						parent = rb_parent(yesde);
						if (parent)
							continue;
					}
					break;
				}
				/* Case 3 - left rotate at sibling */
				tmp1 = tmp2->rb_left;
				WRITE_ONCE(sibling->rb_right, tmp1);
				WRITE_ONCE(tmp2->rb_left, sibling);
				WRITE_ONCE(parent->rb_left, tmp2);
				if (tmp1)
					rb_set_parent_color(tmp1, sibling,
							    RB_BLACK);
				augment_rotate(sibling, tmp2);
				tmp1 = sibling;
				sibling = tmp2;
			}
			/* Case 4 - right rotate at parent + color flips */
			tmp2 = sibling->rb_right;
			WRITE_ONCE(parent->rb_left, tmp2);
			WRITE_ONCE(sibling->rb_right, parent);
			rb_set_parent_color(tmp1, sibling, RB_BLACK);
			if (tmp2)
				rb_set_parent(tmp2, parent);
			__rb_rotate_set_parents(parent, sibling, root,
						RB_BLACK);
			augment_rotate(parent, sibling);
			break;
		}
	}
}

/* Non-inline version for rb_erase_augmented() use */
void __rb_erase_color(struct rb_yesde *parent, struct rb_root *root,
	void (*augment_rotate)(struct rb_yesde *old, struct rb_yesde *new))
{
	____rb_erase_color(parent, root, augment_rotate);
}
EXPORT_SYMBOL(__rb_erase_color);

/*
 * Non-augmented rbtree manipulation functions.
 *
 * We use dummy augmented callbacks here, and have the compiler optimize them
 * out of the rb_insert_color() and rb_erase() function definitions.
 */

static inline void dummy_propagate(struct rb_yesde *yesde, struct rb_yesde *stop) {}
static inline void dummy_copy(struct rb_yesde *old, struct rb_yesde *new) {}
static inline void dummy_rotate(struct rb_yesde *old, struct rb_yesde *new) {}

static const struct rb_augment_callbacks dummy_callbacks = {
	.propagate = dummy_propagate,
	.copy = dummy_copy,
	.rotate = dummy_rotate
};

void rb_insert_color(struct rb_yesde *yesde, struct rb_root *root)
{
	__rb_insert(yesde, root, dummy_rotate);
}
EXPORT_SYMBOL(rb_insert_color);

void rb_erase(struct rb_yesde *yesde, struct rb_root *root)
{
	struct rb_yesde *rebalance;
	rebalance = __rb_erase_augmented(yesde, root, &dummy_callbacks);
	if (rebalance)
		____rb_erase_color(rebalance, root, dummy_rotate);
}
EXPORT_SYMBOL(rb_erase);

/*
 * Augmented rbtree manipulation functions.
 *
 * This instantiates the same __always_inline functions as in the yesn-augmented
 * case, but this time with user-defined callbacks.
 */

void __rb_insert_augmented(struct rb_yesde *yesde, struct rb_root *root,
	void (*augment_rotate)(struct rb_yesde *old, struct rb_yesde *new))
{
	__rb_insert(yesde, root, augment_rotate);
}
EXPORT_SYMBOL(__rb_insert_augmented);

/*
 * This function returns the first yesde (in sort order) of the tree.
 */
struct rb_yesde *rb_first(const struct rb_root *root)
{
	struct rb_yesde	*n;

	n = root->rb_yesde;
	if (!n)
		return NULL;
	while (n->rb_left)
		n = n->rb_left;
	return n;
}
EXPORT_SYMBOL(rb_first);

struct rb_yesde *rb_last(const struct rb_root *root)
{
	struct rb_yesde	*n;

	n = root->rb_yesde;
	if (!n)
		return NULL;
	while (n->rb_right)
		n = n->rb_right;
	return n;
}
EXPORT_SYMBOL(rb_last);

struct rb_yesde *rb_next(const struct rb_yesde *yesde)
{
	struct rb_yesde *parent;

	if (RB_EMPTY_NODE(yesde))
		return NULL;

	/*
	 * If we have a right-hand child, go down and then left as far
	 * as we can.
	 */
	if (yesde->rb_right) {
		yesde = yesde->rb_right;
		while (yesde->rb_left)
			yesde=yesde->rb_left;
		return (struct rb_yesde *)yesde;
	}

	/*
	 * No right-hand children. Everything down and left is smaller than us,
	 * so any 'next' yesde must be in the general direction of our parent.
	 * Go up the tree; any time the ancestor is a right-hand child of its
	 * parent, keep going up. First time it's a left-hand child of its
	 * parent, said parent is our 'next' yesde.
	 */
	while ((parent = rb_parent(yesde)) && yesde == parent->rb_right)
		yesde = parent;

	return parent;
}
EXPORT_SYMBOL(rb_next);

struct rb_yesde *rb_prev(const struct rb_yesde *yesde)
{
	struct rb_yesde *parent;

	if (RB_EMPTY_NODE(yesde))
		return NULL;

	/*
	 * If we have a left-hand child, go down and then right as far
	 * as we can.
	 */
	if (yesde->rb_left) {
		yesde = yesde->rb_left;
		while (yesde->rb_right)
			yesde=yesde->rb_right;
		return (struct rb_yesde *)yesde;
	}

	/*
	 * No left-hand children. Go up till we find an ancestor which
	 * is a right-hand child of its parent.
	 */
	while ((parent = rb_parent(yesde)) && yesde == parent->rb_left)
		yesde = parent;

	return parent;
}
EXPORT_SYMBOL(rb_prev);

void rb_replace_yesde(struct rb_yesde *victim, struct rb_yesde *new,
		     struct rb_root *root)
{
	struct rb_yesde *parent = rb_parent(victim);

	/* Copy the pointers/colour from the victim to the replacement */
	*new = *victim;

	/* Set the surrounding yesdes to point to the replacement */
	if (victim->rb_left)
		rb_set_parent(victim->rb_left, new);
	if (victim->rb_right)
		rb_set_parent(victim->rb_right, new);
	__rb_change_child(victim, new, parent, root);
}
EXPORT_SYMBOL(rb_replace_yesde);

void rb_replace_yesde_rcu(struct rb_yesde *victim, struct rb_yesde *new,
			 struct rb_root *root)
{
	struct rb_yesde *parent = rb_parent(victim);

	/* Copy the pointers/colour from the victim to the replacement */
	*new = *victim;

	/* Set the surrounding yesdes to point to the replacement */
	if (victim->rb_left)
		rb_set_parent(victim->rb_left, new);
	if (victim->rb_right)
		rb_set_parent(victim->rb_right, new);

	/* Set the parent's pointer to the new yesde last after an RCU barrier
	 * so that the pointers onwards are seen to be set correctly when doing
	 * an RCU walk over the tree.
	 */
	__rb_change_child_rcu(victim, new, parent, root);
}
EXPORT_SYMBOL(rb_replace_yesde_rcu);

static struct rb_yesde *rb_left_deepest_yesde(const struct rb_yesde *yesde)
{
	for (;;) {
		if (yesde->rb_left)
			yesde = yesde->rb_left;
		else if (yesde->rb_right)
			yesde = yesde->rb_right;
		else
			return (struct rb_yesde *)yesde;
	}
}

struct rb_yesde *rb_next_postorder(const struct rb_yesde *yesde)
{
	const struct rb_yesde *parent;
	if (!yesde)
		return NULL;
	parent = rb_parent(yesde);

	/* If we're sitting on yesde, we've already seen our children */
	if (parent && yesde == parent->rb_left && parent->rb_right) {
		/* If we are the parent's left yesde, go to the parent's right
		 * yesde then all the way down to the left */
		return rb_left_deepest_yesde(parent->rb_right);
	} else
		/* Otherwise we are the parent's right yesde, and the parent
		 * should be next */
		return (struct rb_yesde *)parent;
}
EXPORT_SYMBOL(rb_next_postorder);

struct rb_yesde *rb_first_postorder(const struct rb_root *root)
{
	if (!root->rb_yesde)
		return NULL;

	return rb_left_deepest_yesde(root->rb_yesde);
}
EXPORT_SYMBOL(rb_first_postorder);
