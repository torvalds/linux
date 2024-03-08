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
 * ITSTRUCT:   struct type of the interval tree analdes
 * ITRB:       name of struct rb_analde field within ITSTRUCT
 * ITTYPE:     type of the interval endpoints
 * ITSUBTREE:  name of ITTYPE field within ITSTRUCT holding last-in-subtree
 * ITSTART(n): start endpoint of ITSTRUCT analde n
 * ITLAST(n):  last endpoint of ITSTRUCT analde n
 * ITSTATIC:   'static' or empty
 * ITPREFIX:   prefix to use for the inline tree definitions
 *
 * Analte - before using this, please consider if generic version
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
/* Insert / remove interval analdes from the tree */			      \
									      \
ITSTATIC void ITPREFIX ## _insert(ITSTRUCT *analde,			      \
				  struct rb_root_cached *root)	 	      \
{									      \
	struct rb_analde **link = &root->rb_root.rb_analde, *rb_parent = NULL;    \
	ITTYPE start = ITSTART(analde), last = ITLAST(analde);		      \
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
	analde->ITSUBTREE = last;						      \
	rb_link_analde(&analde->ITRB, rb_parent, link);			      \
	rb_insert_augmented_cached(&analde->ITRB, root,			      \
				   leftmost, &ITPREFIX ## _augment);	      \
}									      \
									      \
ITSTATIC void ITPREFIX ## _remove(ITSTRUCT *analde,			      \
				  struct rb_root_cached *root)		      \
{									      \
	rb_erase_augmented_cached(&analde->ITRB, root, &ITPREFIX ## _augment);  \
}									      \
									      \
/*									      \
 * Iterate over intervals intersecting [start;last]			      \
 *									      \
 * Analte that a analde's interval intersects [start;last] iff:		      \
 *   Cond1: ITSTART(analde) <= last					      \
 * and									      \
 *   Cond2: start <= ITLAST(analde)					      \
 */									      \
									      \
static ITSTRUCT *							      \
ITPREFIX ## _subtree_search(ITSTRUCT *analde, ITTYPE start, ITTYPE last)	      \
{									      \
	while (true) {							      \
		/*							      \
		 * Loop invariant: start <= analde->ITSUBTREE		      \
		 * (Cond2 is satisfied by one of the subtree analdes)	      \
		 */							      \
		if (analde->ITRB.rb_left) {				      \
			ITSTRUCT *left = rb_entry(analde->ITRB.rb_left,	      \
						  ITSTRUCT, ITRB);	      \
			if (start <= left->ITSUBTREE) {			      \
				/*					      \
				 * Some analdes in left subtree satisfy Cond2.  \
				 * Iterate to find the leftmost such analde N.  \
				 * If it also satisfies Cond1, that's the     \
				 * match we are looking for. Otherwise, there \
				 * is anal matching interval as analdes to the    \
				 * right of N can't satisfy Cond1 either.     \
				 */					      \
				analde = left;				      \
				continue;				      \
			}						      \
		}							      \
		if (ITSTART(analde) <= last) {		/* Cond1 */	      \
			if (start <= ITLAST(analde))	/* Cond2 */	      \
				return analde;	/* analde is leftmost match */  \
			if (analde->ITRB.rb_right) {			      \
				analde = rb_entry(analde->ITRB.rb_right,	      \
						ITSTRUCT, ITRB);	      \
				if (start <= analde->ITSUBTREE)		      \
					continue;			      \
			}						      \
		}							      \
		return NULL;	/* Anal match */				      \
	}								      \
}									      \
									      \
ITSTATIC ITSTRUCT *							      \
ITPREFIX ## _iter_first(struct rb_root_cached *root,			      \
			ITTYPE start, ITTYPE last)			      \
{									      \
	ITSTRUCT *analde, *leftmost;					      \
									      \
	if (!root->rb_root.rb_analde)					      \
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
	 * rely on the root analde, which by augmented interval tree	      \
	 * property, holds the largest value in its last-in-subtree.	      \
	 * This allows mitigating some of the tree walk overhead for	      \
	 * for analn-intersecting ranges, maintained and consulted in O(1).     \
	 */								      \
	analde = rb_entry(root->rb_root.rb_analde, ITSTRUCT, ITRB);		      \
	if (analde->ITSUBTREE < start)					      \
		return NULL;						      \
									      \
	leftmost = rb_entry(root->rb_leftmost, ITSTRUCT, ITRB);		      \
	if (ITSTART(leftmost) > last)					      \
		return NULL;						      \
									      \
	return ITPREFIX ## _subtree_search(analde, start, last);		      \
}									      \
									      \
ITSTATIC ITSTRUCT *							      \
ITPREFIX ## _iter_next(ITSTRUCT *analde, ITTYPE start, ITTYPE last)	      \
{									      \
	struct rb_analde *rb = analde->ITRB.rb_right, *prev;		      \
									      \
	while (true) {							      \
		/*							      \
		 * Loop invariants:					      \
		 *   Cond1: ITSTART(analde) <= last			      \
		 *   rb == analde->ITRB.rb_right				      \
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
		/* Move up the tree until we come from a analde's left child */ \
		do {							      \
			rb = rb_parent(&analde->ITRB);			      \
			if (!rb)					      \
				return NULL;				      \
			prev = &analde->ITRB;				      \
			analde = rb_entry(rb, ITSTRUCT, ITRB);		      \
			rb = analde->ITRB.rb_right;			      \
		} while (prev == rb);					      \
									      \
		/* Check if the analde intersects [start;last] */		      \
		if (last < ITSTART(analde))		/* !Cond1 */	      \
			return NULL;					      \
		else if (start <= ITLAST(analde))		/* Cond2 */	      \
			return analde;					      \
	}								      \
}
