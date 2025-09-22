/*
 * rbtree.h -- generic red-black tree
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#ifndef RBTREE_H
#define	RBTREE_H

#include "region-allocator.h"

/*
 * This structure must be the first member of the data structure in
 * the rbtree.  This allows easy casting between an rbnode_type and the
 * user data (poor man's inheritance).
 */
typedef struct rbnode rbnode_type;
struct rbnode {
	rbnode_type  *parent;
	rbnode_type  *left;
	rbnode_type  *right;
	const void   *key;
	uint8_t	      color;
} ATTR_PACKED;

#define	RBTREE_NULL &rbtree_null_node
extern	rbnode_type	rbtree_null_node;

typedef struct rbtree rbtree_type;
struct rbtree {
	region_type *region;

	/* The root of the red-black tree */
	rbnode_type *root;

	/* The number of the nodes in the tree */
	size_t       count;

	/* Current node for walks... */
	rbnode_type *_node;

	/* Key compare function. <0,0,>0 like strcmp. Return 0 on two NULL ptrs. */
	int (*cmp) (const void *, const void *);
} ATTR_PACKED;

/* rbtree.c */
rbtree_type *rbtree_create(region_type *region, int (*cmpf)(const void *, const void *));
rbnode_type *rbtree_insert(rbtree_type *rbtree, rbnode_type *data);
/* returns node that is now unlinked from the tree. User to delete it. 
 * returns 0 if node not present */
rbnode_type *rbtree_delete(rbtree_type *rbtree, const void *key);
rbnode_type *rbtree_search(rbtree_type *rbtree, const void *key);
/* returns true if exact match in result. Else result points to <= element,
   or NULL if key is smaller than the smallest key. */
int rbtree_find_less_equal(rbtree_type *rbtree, const void *key, rbnode_type **result);
rbnode_type *rbtree_first(rbtree_type *rbtree);
rbnode_type *rbtree_last(rbtree_type *rbtree);
rbnode_type *rbtree_next(rbnode_type *rbtree);
rbnode_type *rbtree_previous(rbnode_type *rbtree);

#define	RBTREE_WALK(rbtree, k, d) \
	for((rbtree)->_node = rbtree_first(rbtree);\
		(rbtree)->_node != RBTREE_NULL && ((k) = (rbtree)->_node->key) && \
		((d) = (void *) (rbtree)->_node); (rbtree)->_node = rbtree_next((rbtree)->_node))

/* call with node=variable of struct* with rbnode_type as first element.
   with type is the type of a pointer to that struct. */
#define RBTREE_FOR(node, type, rbtree) \
	for(node=(type)rbtree_first(rbtree); \
		(rbnode_type*)node != RBTREE_NULL; \
		node = (type)rbtree_next((rbnode_type*)node))

#endif /* RBTREE_H */
