/*
  Interval Trees
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

  include/linux/interval_tree_generic.h
*/

#include <linux/rbtree_augmented.h>

/*
 * Template for implementing interval trees
 *
 * ITSTRUCT:   struct type of the interval tree nodes
 * ITRB:       name of struct rb_node field within ITSTRUCT
 * ITTYPE:     type of the interval endpoints
 * ITSUBTREE:  name of ITTYPE field within ITSTRUCT holding last-in-subtree
 * ITSTART(n): start endpoint of ITSTRUCT node n
 * ITLAST(n):  last endpoint of ITSTRUCT node n
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
static inline ITTYPE ITPREFIX ## _compute_subtree_last(ITSTRUCT *node)	      \
{									      \
	ITTYPE max = ITLAST(node), subtree_last;			      \
	if (node->ITRB.rb_left) {					      \
		subtree_last = rb_entry(node->ITRB.rb_left,		      \
					ITSTRUCT, ITRB)->ITSUBTREE;	      \
		if (max < subtree_last)					      \
			max = subtree_last;				      \
	}								      \
	if (node->ITRB.rb_right) {					      \
		subtree_last = rb_entry(node->ITRB.rb_right,		      \
					ITSTRUCT, ITRB)->ITSUBTREE;	      \
		if (max < subtree_last)					      \
			max = subtree_last;				      \
	}								      \
	return max;							      \
}									      \
									      \
RB_DECLARE_CALLBACKS(static, ITPREFIX ## _augment, ITSTRUCT, ITRB,	      \
		     ITTYPE, ITSUBTREE, ITPREFIX ## _compute_subtree_last)    \
									      \
/* Insert / remove interval nodes from the tree */			      \
									      \
ITSTATIC void ITPREFIX ## _insert(ITSTRUCT *node,			      \
				  struct rb_root_cached *root)	 	      \
{									      \
	struct rb_node **link = &root->rb_root.rb_node, *rb_parent = NULL;    \
	ITTYPE start = ITSTART(node), last = ITLAST(node);		      \
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
	node->ITSUBTREE = last;						      \
	rb_link_node(&node->ITRB, rb_parent, link);			      \
	rb_insert_augmented_cached(&node->ITRB, root,			      \
				   leftmost, &ITPREFIX ## _augment);	      \
}									      \
									      \
ITSTATIC void ITPREFIX ## _remove(ITSTRUCT *node,			      \
				  struct rb_root_cached *root)		      \
{									      \
	rb_erase_augmented_cached(&node->ITRB, root, &ITPREFIX ## _augment);  \
}									      \
									      \
/*									      \
 * Iterate over intervals intersecting [start;last]			      \
 *									      \
 * Note that a node's interval intersects [start;last] iff:		      \
 *   Cond1: ITSTART(node) <= last					      \
 * and									      \
 *   Cond2: start <= ITLAST(node)					      \
 */									      \
									      \
static ITSTRUCT *							      \
ITPREFIX ## _subtree_search(ITSTRUCT *node, ITTYPE start, ITTYPE last)	      \
{									      \
	while (true) {							      \
		/*							      \
		 * Loop invariant: start <= node->ITSUBTREE		      \
		 * (Cond2 is satisfied by one of the subtree nodes)	      \
		 */							      \
		if (node->ITRB.rb_left) {				      \
			ITSTRUCT *left = rb_entry(node->ITRB.rb_left,	      \
						  ITSTRUCT, ITRB);	      \
			if (start <= left->ITSUBTREE) {			      \
				/*					      \
				 * Some nodes in left subtree satisfy Cond2.  \
				 * Iterate to find the leftmost such node N.  \
				 * If it also satisfies Cond1, that's the     \
				 * match we are looking for. Otherwise, there \
				 * is no matching interval as nodes to the    \
				 * right of N can't satisfy Cond1 either.     \
				 */					      \
				node = left;				      \
				continue;				      \
			}						      \
		}							      \
		if (ITSTART(node) <= last) {		/* Cond1 */	      \
			if (start <= ITLAST(node))	/* Cond2 */	      \
				return node;	/* node is leftmost match */  \
			if (node->ITRB.rb_right) {			      \
				node = rb_entry(node->ITRB.rb_right,	      \
						ITSTRUCT, ITRB);	      \
				if (start <= node->ITSUBTREE)		      \
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
	ITSTRUCT *node, *leftmost;					      \
									      \
	if (!root->rb_root.rb_node)					      \
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
	 * rely on the root node, which by augmented interval tree	      \
	 * property, holds the largest value in its last-in-subtree.	      \
	 * This allows mitigating some of the tree walk overhead for	      \
	 * for non-intersecting ranges, maintained and consulted in O(1).     \
	 */								      \
	node = rb_entry(root->rb_root.rb_node, ITSTRUCT, ITRB);		      \
	if (node->ITSUBTREE < start)					      \
		return NULL;						      \
									      \
	leftmost = rb_entry(root->rb_leftmost, ITSTRUCT, ITRB);		      \
	if (ITSTART(leftmost) > last)					      \
		return NULL;						      \
									      \
	return ITPREFIX ## _subtree_search(node, start, last);		      \
}									      \
									      \
ITSTATIC ITSTRUCT *							      \
ITPREFIX ## _iter_next(ITSTRUCT *node, ITTYPE start, ITTYPE last)	      \
{									      \
	struct rb_node *rb = node->ITRB.rb_right, *prev;		      \
									      \
	while (true) {							      \
		/*							      \
		 * Loop invariants:					      \
		 *   Cond1: ITSTART(node) <= last			      \
		 *   rb == node->ITRB.rb_right				      \
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
		/* Move up the tree until we come from a node's left child */ \
		do {							      \
			rb = rb_parent(&node->ITRB);			      \
			if (!rb)					      \
				return NULL;				      \
			prev = &node->ITRB;				      \
			node = rb_entry(rb, ITSTRUCT, ITRB);		      \
			rb = node->ITRB.rb_right;			      \
		} while (prev == rb);					      \
									      \
		/* Check if the node intersects [start;last] */		      \
		if (last < ITSTART(node))		/* !Cond1 */	      \
			return NULL;					      \
		else if (start <= ITLAST(node))		/* Cond2 */	      \
			return node;					      \
	}								      \
}
