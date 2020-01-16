/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
  Interval Trees
  (C) 2012  Michel Lespinasse <walken@google.com>


  include/linux/interval_tree_generic.h
*/

#include <linux/rbtree_augmented.h>

/*
 * Template for implementing interval trees
 *
 * ITSTRUCT:   struct type of the interval tree yesdes
 * ITRB:       name of struct rb_yesde field within ITSTRUCT
 * ITTYPE:     type of the interval endpoints
 * ITSUBTREE:  name of ITTYPE field within ITSTRUCT holding last-in-subtree
 * ITSTART(n): start endpoint of ITSTRUCT yesde n
 * ITLAST(n):  last endpoint of ITSTRUCT yesde n
 * ITSTATIC:   'static' or empty
 * ITPREFIX:   prefix to use for the inline tree definitions
 *
 * Note - before using this, please consider if generic version
 * (interval_tree.h) would work for you...
 */

#define INTERVAL_TREE_DEFINE(ITSTRUCT, ITRB, ITTYPE, ITSUBTREE,		      \
			     ITSTART, ITLAST, ITSTATIC, ITPREFIX)	      \
									      \
/* Callbacks for augmented rbtree insert and remove */			      \
									      \
RB_DECLARE_CALLBACKS_MAX(static, ITPREFIX ## _augment,			      \
			 ITSTRUCT, ITRB, ITTYPE, ITSUBTREE, ITLAST)	      \
									      \
/* Insert / remove interval yesdes from the tree */			      \
									      \
ITSTATIC void ITPREFIX ## _insert(ITSTRUCT *yesde,			      \
				  struct rb_root_cached *root)	 	      \
{									      \
	struct rb_yesde **link = &root->rb_root.rb_yesde, *rb_parent = NULL;    \
	ITTYPE start = ITSTART(yesde), last = ITLAST(yesde);		      \
	ITSTRUCT *parent;						      \
	bool leftmost = true;						      \
									      \
	while (*link) {							      \
		rb_parent = *link;					      \
		parent = rb_entry(rb_parent, ITSTRUCT, ITRB);		      \
		if (parent->ITSUBTREE < last)				      \
			parent->ITSUBTREE = last;			      \
		if (start < ITSTART(parent))				      \
			link = &parent->ITRB.rb_left;			      \
		else {							      \
			link = &parent->ITRB.rb_right;			      \
			leftmost = false;				      \
		}							      \
	}								      \
									      \
	yesde->ITSUBTREE = last;						      \
	rb_link_yesde(&yesde->ITRB, rb_parent, link);			      \
	rb_insert_augmented_cached(&yesde->ITRB, root,			      \
				   leftmost, &ITPREFIX ## _augment);	      \
}									      \
									      \
ITSTATIC void ITPREFIX ## _remove(ITSTRUCT *yesde,			      \
				  struct rb_root_cached *root)		      \
{									      \
	rb_erase_augmented_cached(&yesde->ITRB, root, &ITPREFIX ## _augment);  \
}									      \
									      \
/*									      \
 * Iterate over intervals intersecting [start;last]			      \
 *									      \
 * Note that a yesde's interval intersects [start;last] iff:		      \
 *   Cond1: ITSTART(yesde) <= last					      \
 * and									      \
 *   Cond2: start <= ITLAST(yesde)					      \
 */									      \
									      \
static ITSTRUCT *							      \
ITPREFIX ## _subtree_search(ITSTRUCT *yesde, ITTYPE start, ITTYPE last)	      \
{									      \
	while (true) {							      \
		/*							      \
		 * Loop invariant: start <= yesde->ITSUBTREE		      \
		 * (Cond2 is satisfied by one of the subtree yesdes)	      \
		 */							      \
		if (yesde->ITRB.rb_left) {				      \
			ITSTRUCT *left = rb_entry(yesde->ITRB.rb_left,	      \
						  ITSTRUCT, ITRB);	      \
			if (start <= left->ITSUBTREE) {			      \
				/*					      \
				 * Some yesdes in left subtree satisfy Cond2.  \
				 * Iterate to find the leftmost such yesde N.  \
				 * If it also satisfies Cond1, that's the     \
				 * match we are looking for. Otherwise, there \
				 * is yes matching interval as yesdes to the    \
				 * right of N can't satisfy Cond1 either.     \
				 */					      \
				yesde = left;				      \
				continue;				      \
			}						      \
		}							      \
		if (ITSTART(yesde) <= last) {		/* Cond1 */	      \
			if (start <= ITLAST(yesde))	/* Cond2 */	      \
				return yesde;	/* yesde is leftmost match */  \
			if (yesde->ITRB.rb_right) {			      \
				yesde = rb_entry(yesde->ITRB.rb_right,	      \
						ITSTRUCT, ITRB);	      \
				if (start <= yesde->ITSUBTREE)		      \
					continue;			      \
			}						      \
		}							      \
		return NULL;	/* No match */				      \
	}								      \
}									      \
									      \
ITSTATIC ITSTRUCT *							      \
ITPREFIX ## _iter_first(struct rb_root_cached *root,			      \
			ITTYPE start, ITTYPE last)			      \
{									      \
	ITSTRUCT *yesde, *leftmost;					      \
									      \
	if (!root->rb_root.rb_yesde)					      \
		return NULL;						      \
									      \
	/*								      \
	 * Fastpath range intersection/overlap between A: [a0, a1] and	      \
	 * B: [b0, b1] is given by:					      \
	 *								      \
	 *         a0 <= b1 && b0 <= a1					      \
	 *								      \
	 *  ... where A holds the lock range and B holds the smallest	      \
	 * 'start' and largest 'last' in the tree. For the later, we	      \
	 * rely on the root yesde, which by augmented interval tree	      \
	 * property, holds the largest value in its last-in-subtree.	      \
	 * This allows mitigating some of the tree walk overhead for	      \
	 * for yesn-intersecting ranges, maintained and consulted in O(1).     \
	 */								      \
	yesde = rb_entry(root->rb_root.rb_yesde, ITSTRUCT, ITRB);		      \
	if (yesde->ITSUBTREE < start)					      \
		return NULL;						      \
									      \
	leftmost = rb_entry(root->rb_leftmost, ITSTRUCT, ITRB);		      \
	if (ITSTART(leftmost) > last)					      \
		return NULL;						      \
									      \
	return ITPREFIX ## _subtree_search(yesde, start, last);		      \
}									      \
									      \
ITSTATIC ITSTRUCT *							      \
ITPREFIX ## _iter_next(ITSTRUCT *yesde, ITTYPE start, ITTYPE last)	      \
{									      \
	struct rb_yesde *rb = yesde->ITRB.rb_right, *prev;		      \
									      \
	while (true) {							      \
		/*							      \
		 * Loop invariants:					      \
		 *   Cond1: ITSTART(yesde) <= last			      \
		 *   rb == yesde->ITRB.rb_right				      \
		 *							      \
		 * First, search right subtree if suitable		      \
		 */							      \
		if (rb) {						      \
			ITSTRUCT *right = rb_entry(rb, ITSTRUCT, ITRB);	      \
			if (start <= right->ITSUBTREE)			      \
				return ITPREFIX ## _subtree_search(right,     \
								start, last); \
		}							      \
									      \
		/* Move up the tree until we come from a yesde's left child */ \
		do {							      \
			rb = rb_parent(&yesde->ITRB);			      \
			if (!rb)					      \
				return NULL;				      \
			prev = &yesde->ITRB;				      \
			yesde = rb_entry(rb, ITSTRUCT, ITRB);		      \
			rb = yesde->ITRB.rb_right;			      \
		} while (prev == rb);					      \
									      \
		/* Check if the yesde intersects [start;last] */		      \
		if (last < ITSTART(yesde))		/* !Cond1 */	      \
			return NULL;					      \
		else if (start <= ITLAST(yesde))		/* Cond2 */	      \
			return yesde;					      \
	}								      \
}
