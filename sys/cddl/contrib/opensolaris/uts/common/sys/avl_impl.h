/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_AVL_IMPL_H
#define	_AVL_IMPL_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * This is a private header file.  Applications should not directly include
 * this file.
 */

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif


/*
 * generic AVL tree implementation for kernel use
 *
 * There are 5 pieces of information stored for each node in an AVL tree
 *
 * 	pointer to less than child
 * 	pointer to greater than child
 * 	a pointer to the parent of this node
 *	an indication  [0/1]  of which child I am of my parent
 * 	a "balance" (-1, 0, +1)  indicating which child tree is taller
 *
 * Since they only need 3 bits, the last two fields are packed into the
 * bottom bits of the parent pointer on 64 bit machines to save on space.
 */

#ifndef _LP64

struct avl_node {
	struct avl_node *avl_child[2];	/* left/right children */
	struct avl_node *avl_parent;	/* this node's parent */
	unsigned short avl_child_index;	/* my index in parent's avl_child[] */
	short avl_balance;		/* balance value: -1, 0, +1 */
};

#define	AVL_XPARENT(n)		((n)->avl_parent)
#define	AVL_SETPARENT(n, p)	((n)->avl_parent = (p))

#define	AVL_XCHILD(n)		((n)->avl_child_index)
#define	AVL_SETCHILD(n, c)	((n)->avl_child_index = (unsigned short)(c))

#define	AVL_XBALANCE(n)		((n)->avl_balance)
#define	AVL_SETBALANCE(n, b)	((n)->avl_balance = (short)(b))

#else /* _LP64 */

/*
 * for 64 bit machines, avl_pcb contains parent pointer, balance and child_index
 * values packed in the following manner:
 *
 * |63                                  3|        2        |1          0 |
 * |-------------------------------------|-----------------|-------------|
 * |      avl_parent hi order bits       | avl_child_index | avl_balance |
 * |                                     |                 |     + 1     |
 * |-------------------------------------|-----------------|-------------|
 *
 */
struct avl_node {
	struct avl_node *avl_child[2];	/* left/right children nodes */
	uintptr_t avl_pcb;		/* parent, child_index, balance */
};

/*
 * macros to extract/set fields in avl_pcb
 *
 * pointer to the parent of the current node is the high order bits
 */
#define	AVL_XPARENT(n)		((struct avl_node *)((n)->avl_pcb & ~7))
#define	AVL_SETPARENT(n, p)						\
	((n)->avl_pcb = (((n)->avl_pcb & 7) | (uintptr_t)(p)))

/*
 * index of this node in its parent's avl_child[]: bit #2
 */
#define	AVL_XCHILD(n)		(((n)->avl_pcb >> 2) & 1)
#define	AVL_SETCHILD(n, c)						\
	((n)->avl_pcb = (uintptr_t)(((n)->avl_pcb & ~4) | ((c) << 2)))

/*
 * balance indication for a node, lowest 2 bits. A valid balance is
 * -1, 0, or +1, and is encoded by adding 1 to the value to get the
 * unsigned values of 0, 1, 2.
 */
#define	AVL_XBALANCE(n)		((int)(((n)->avl_pcb & 3) - 1))
#define	AVL_SETBALANCE(n, b)						\
	((n)->avl_pcb = (uintptr_t)((((n)->avl_pcb & ~3) | ((b) + 1))))

#endif /* _LP64 */



/*
 * switch between a node and data pointer for a given tree
 * the value of "o" is tree->avl_offset
 */
#define	AVL_NODE2DATA(n, o)	((void *)((uintptr_t)(n) - (o)))
#define	AVL_DATA2NODE(d, o)	((struct avl_node *)((uintptr_t)(d) + (o)))



/*
 * macros used to create/access an avl_index_t
 */
#define	AVL_INDEX2NODE(x)	((avl_node_t *)((x) & ~1))
#define	AVL_INDEX2CHILD(x)	((x) & 1)
#define	AVL_MKINDEX(n, c)	((avl_index_t)(n) | (c))


/*
 * The tree structure. The fields avl_root, avl_compar, and avl_offset come
 * first since they are needed for avl_find().  We want them to fit into
 * a single 64 byte cache line to make avl_find() as fast as possible.
 */
struct avl_tree {
	struct avl_node *avl_root;	/* root node in tree */
	int (*avl_compar)(const void *, const void *);
	size_t avl_offset;		/* offsetof(type, avl_link_t field) */
	ulong_t avl_numnodes;		/* number of nodes in the tree */
	size_t avl_size;		/* sizeof user type struct */
};


/*
 * This will only by used via AVL_NEXT() or AVL_PREV()
 */
extern void *avl_walk(struct avl_tree *, void *, int);

#ifdef	__cplusplus
}
#endif

#endif	/* _AVL_IMPL_H */
