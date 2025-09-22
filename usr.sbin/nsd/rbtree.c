/*
 * rbtree.c -- generic red black tree
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#include "config.h"

#include <assert.h>
#include <stdlib.h>

#include "rbtree.h"

#define	BLACK	0
#define	RED	1

rbnode_type	rbtree_null_node = {
	RBTREE_NULL,		/* Parent.  */
	RBTREE_NULL,		/* Left.  */
	RBTREE_NULL,		/* Right.  */
	NULL,			/* Key.  */
	BLACK			/* Color.  */
};

static void rbtree_rotate_left(rbtree_type *rbtree, rbnode_type *node);
static void rbtree_rotate_right(rbtree_type *rbtree, rbnode_type *node);
static void rbtree_insert_fixup(rbtree_type *rbtree, rbnode_type *node);
static void rbtree_delete_fixup(rbtree_type* rbtree, rbnode_type* child, rbnode_type* child_parent);

/*
 * Creates a new red black tree, initializes and returns a pointer to it.
 *
 * Return NULL on failure.
 *
 */
rbtree_type *
rbtree_create (region_type *region, int (*cmpf)(const void *, const void *))
{
	rbtree_type *rbtree;

	/* Allocate memory for it */
	rbtree = (rbtree_type *) region_alloc(region, sizeof(rbtree_type));
	if (!rbtree) {
		return NULL;
	}

	/* Initialize it */
	rbtree->root = RBTREE_NULL;
	rbtree->count = 0;
	rbtree->region = region;
	rbtree->cmp = cmpf;

	return rbtree;
}

/*
 * Rotates the node to the left.
 *
 */
static void
rbtree_rotate_left(rbtree_type *rbtree, rbnode_type *node)
{
	rbnode_type *right;

	/* Check if rbtree is NULL */
	if (!rbtree) {
		return;
	}
	right = node->right;
	node->right = right->left;
	if (right->left != RBTREE_NULL)
		right->left->parent = node;

	right->parent = node->parent;

	if (node->parent != RBTREE_NULL) {
		if (node == node->parent->left) {
			node->parent->left = right;
		} else  {
			node->parent->right = right;
		}
	} else {
		rbtree->root = right;
	}
	right->left = node;
	node->parent = right;
}

/*
 * Rotates the node to the right.
 *
 */
static void
rbtree_rotate_right(rbtree_type *rbtree, rbnode_type *node)
{
	rbnode_type *left;

	/* Check if rbtree is NULL */
	if (!rbtree) {
		return;
	}
	left = node->left;
	node->left = left->right;
	if (left->right != RBTREE_NULL)
		left->right->parent = node;

	left->parent = node->parent;

	if (node->parent != RBTREE_NULL) {
		if (node == node->parent->right) {
			node->parent->right = left;
		} else  {
			node->parent->left = left;
		}
	} else {
		rbtree->root = left;
	}
	left->right = node;
	node->parent = left;
}

static void
rbtree_insert_fixup(rbtree_type *rbtree, rbnode_type *node)
{
	rbnode_type	*uncle;

	/* Check if rbtree is NULL */
	if (!rbtree) {
		return;
	}

	/* While not at the root and need fixing... */
	while (node != rbtree->root && node->parent->color == RED) {
		/* If our parent is left child of our grandparent... */
		if (node->parent == node->parent->parent->left) {
			uncle = node->parent->parent->right;

			/* If our uncle is red... */
			if (uncle->color == RED) {
				/* Paint the parent and the uncle black... */
				node->parent->color = BLACK;
				uncle->color = BLACK;

				/* And the grandparent red... */
				node->parent->parent->color = RED;

				/* And continue fixing the grandparent */
				node = node->parent->parent;
			} else {				/* Our uncle is black... */
				/* Are we the right child? */
				if (node == node->parent->right) {
					node = node->parent;
					rbtree_rotate_left(rbtree, node);
				}
				/* Now we're the left child, repaint and rotate... */
				node->parent->color = BLACK;
				node->parent->parent->color = RED;
				rbtree_rotate_right(rbtree, node->parent->parent);
			}
		} else {
			uncle = node->parent->parent->left;

			/* If our uncle is red... */
			if (uncle->color == RED) {
				/* Paint the parent and the uncle black... */
				node->parent->color = BLACK;
				uncle->color = BLACK;

				/* And the grandparent red... */
				node->parent->parent->color = RED;

				/* And continue fixing the grandparent */
				node = node->parent->parent;
			} else {				/* Our uncle is black... */
				/* Are we the right child? */
				if (node == node->parent->left) {
					node = node->parent;
					rbtree_rotate_right(rbtree, node);
				}
				/* Now we're the right child, repaint and rotate... */
				node->parent->color = BLACK;
				node->parent->parent->color = RED;
				rbtree_rotate_left(rbtree, node->parent->parent);
			}
		}
	}
	rbtree->root->color = BLACK;
}


/*
 * Inserts a node into a red black tree.
 *
 * Returns NULL on failure or the pointer to the newly added node
 * otherwise.
 */
rbnode_type *
rbtree_insert (rbtree_type *rbtree, rbnode_type *data)
{
	/* XXX Not necessary, but keeps compiler quiet... */
	int r = 0;
	rbnode_type	*node;
	rbnode_type	*parent;

	/* Check if rbtree is NULL */
	if (!rbtree) {
		return NULL;
	}

	/* We start at the root of the tree */
	node = rbtree->root;
	parent = RBTREE_NULL;

	/* Lets find the new parent... */
	while (node != RBTREE_NULL) {
		/* Compare two keys, do we have a duplicate? */
		if ((r = rbtree->cmp(data->key, node->key)) == 0) {
			return NULL;
		}
		parent = node;

		if (r < 0) {
			node = node->left;
		} else {
			node = node->right;
		}
	}

	/* Initialize the new node */
	data->parent = parent;
	data->left = data->right = RBTREE_NULL;
	data->color = RED;
	rbtree->count++;

	/* Insert it into the tree... */
	if (parent != RBTREE_NULL) {
		if (r < 0) {
			parent->left = data;
		} else {
			parent->right = data;
		}
	} else {
		rbtree->root = data;
	}

	/* Fix up the red-black properties... */
	rbtree_insert_fixup(rbtree, data);

	return data;
}

/*
 * Searches the red black tree, returns the data if key is found or NULL otherwise.
 *
 */
rbnode_type *
rbtree_search (rbtree_type *rbtree, const void *key)
{
	rbnode_type *node;

	/* Check if rbtree is NULL */
	if (!rbtree) {
		return NULL;
	}

	if (rbtree_find_less_equal(rbtree, key, &node)) {
		return node;
	} else {
		return NULL;
	}
}

/* helpers for delete */
static void swap_int8(uint8_t* x, uint8_t* y)
{
	uint8_t t = *x; *x = *y; *y = t;
}

static void swap_np(rbnode_type** x, rbnode_type** y)
{
	rbnode_type* t = *x; *x = *y; *y = t;
}

static void change_parent_ptr(rbtree_type* rbtree, rbnode_type* parent, rbnode_type* old, rbnode_type* new)
{
	/* Check if rbtree is NULL */
	if (!rbtree) {
		return;
	}
	
	if(parent == RBTREE_NULL)
	{
		assert(rbtree->root == old);
		if(rbtree->root == old) rbtree->root = new;
		return;
	}
	assert(parent->left == old || parent->right == old
		|| parent->left == new || parent->right == new);
	if(parent->left == old) parent->left = new;
	if(parent->right == old) parent->right = new;
}
static void change_child_ptr(rbnode_type* child, rbnode_type* old, rbnode_type* new)
{
	if(child == RBTREE_NULL) return;
	assert(child->parent == old || child->parent == new);
	if(child->parent == old) child->parent = new;
}

rbnode_type*
rbtree_delete(rbtree_type *rbtree, const void *key)
{
	rbnode_type *to_delete;
	rbnode_type *child;
	
	/* Check if rbtree is NULL */
	if (!rbtree) {
		return NULL;
	}
	
	if((to_delete = rbtree_search(rbtree, key)) == 0) return 0;
	rbtree->count--;

	/* make sure we have at most one non-leaf child */
	if(to_delete->left != RBTREE_NULL && to_delete->right != RBTREE_NULL)
	{
		/* swap with smallest from right subtree (or largest from left) */
		rbnode_type *smright = to_delete->right;
		while(smright->left != RBTREE_NULL)
			smright = smright->left;
		/* swap the smright and to_delete elements in the tree,
		 * but the rbnode_type is first part of user data struct
		 * so cannot just swap the keys and data pointers. Instead
		 * readjust the pointers left,right,parent */

		/* swap colors - colors are tied to the position in the tree */
		swap_int8(&to_delete->color, &smright->color);

		/* swap child pointers in parents of smright/to_delete */
		change_parent_ptr(rbtree, to_delete->parent, to_delete, smright);
		if(to_delete->right != smright)
			change_parent_ptr(rbtree, smright->parent, smright, to_delete);

		/* swap parent pointers in children of smright/to_delete */
		change_child_ptr(smright->left, smright, to_delete);
		change_child_ptr(smright->left, smright, to_delete);
		change_child_ptr(smright->right, smright, to_delete);
		change_child_ptr(smright->right, smright, to_delete);
		change_child_ptr(to_delete->left, to_delete, smright);
		if(to_delete->right != smright)
			change_child_ptr(to_delete->right, to_delete, smright);
		if(to_delete->right == smright)
		{
			/* set up so after swap they work */
			to_delete->right = to_delete;
			smright->parent = smright;
		}

		/* swap pointers in to_delete/smright nodes */
		swap_np(&to_delete->parent, &smright->parent);
		swap_np(&to_delete->left, &smright->left);
		swap_np(&to_delete->right, &smright->right);

		/* now delete to_delete (which is at the location where the smright previously was) */
	}
	assert(to_delete->left == RBTREE_NULL || to_delete->right == RBTREE_NULL);

	if(to_delete->left != RBTREE_NULL) child = to_delete->left;
	else child = to_delete->right;

	/* unlink to_delete from the tree, replace to_delete with child */
	change_parent_ptr(rbtree, to_delete->parent, to_delete, child);
	change_child_ptr(child, to_delete, to_delete->parent);

	if(to_delete->color == RED)
	{
		/* if node is red then the child (black) can be swapped in */
	}
	else if(child->color == RED)
	{
		/* change child to BLACK, removing a RED node is no problem */
		if(child!=RBTREE_NULL) child->color = BLACK;
	}
	else rbtree_delete_fixup(rbtree, child, to_delete->parent);

	/* unlink completely */
	to_delete->parent = RBTREE_NULL;
	to_delete->left = RBTREE_NULL;
	to_delete->right = RBTREE_NULL;
	to_delete->color = BLACK;
	return to_delete;
}

static void rbtree_delete_fixup(rbtree_type* rbtree, rbnode_type* child, rbnode_type* child_parent)
{
	rbnode_type* sibling;
	int go_up = 1;

	/* Check if rbtree is NULL */
	if (!rbtree) {
		return;
	}

	/* determine sibling to the node that is one-black short */
	if(child_parent->right == child) sibling = child_parent->left;
	else sibling = child_parent->right;

	while(go_up)
	{
		if(child_parent == RBTREE_NULL)
		{
			/* removed parent==black from root, every path, so ok */
			return;
		}

		if(sibling->color == RED)
		{	/* rotate to get a black sibling */
			child_parent->color = RED;
			sibling->color = BLACK;
			if(child_parent->right == child)
				rbtree_rotate_right(rbtree, child_parent);
			else	rbtree_rotate_left(rbtree, child_parent);
			/* new sibling after rotation */
			if(child_parent->right == child) sibling = child_parent->left;
			else sibling = child_parent->right;
		}

		if(child_parent->color == BLACK
			&& sibling->color == BLACK
			&& sibling->left->color == BLACK
			&& sibling->right->color == BLACK)
		{	/* fixup local with recolor of sibling */
			if(sibling != RBTREE_NULL)
				sibling->color = RED;

			child = child_parent;
			child_parent = child_parent->parent;
			/* prepare to go up, new sibling */
			if(child_parent->right == child) sibling = child_parent->left;
			else sibling = child_parent->right;
		}
		else go_up = 0;
	}

	if(child_parent->color == RED
		&& sibling->color == BLACK
		&& sibling->left->color == BLACK
		&& sibling->right->color == BLACK)
	{
		/* move red to sibling to rebalance */
		if(sibling != RBTREE_NULL)
			sibling->color = RED;
		child_parent->color = BLACK;
		return;
	}
	assert(sibling != RBTREE_NULL);

	/* get a new sibling, by rotating at sibling. See which child
	   of sibling is red */
	if(child_parent->right == child
		&& sibling->color == BLACK
		&& sibling->right->color == RED
		&& sibling->left->color == BLACK)
	{
		sibling->color = RED;
		sibling->right->color = BLACK;
		rbtree_rotate_left(rbtree, sibling);
		/* new sibling after rotation */
		if(child_parent->right == child) sibling = child_parent->left;
		else sibling = child_parent->right;
	}
	else if(child_parent->left == child
		&& sibling->color == BLACK
		&& sibling->left->color == RED
		&& sibling->right->color == BLACK)
	{
		sibling->color = RED;
		sibling->left->color = BLACK;
		rbtree_rotate_right(rbtree, sibling);
		/* new sibling after rotation */
		if(child_parent->right == child) sibling = child_parent->left;
		else sibling = child_parent->right;
	}

	/* now we have a black sibling with a red child. rotate and exchange colors. */
	sibling->color = child_parent->color;
	child_parent->color = BLACK;
	if(child_parent->right == child)
	{
		assert(sibling->left->color == RED);
		sibling->left->color = BLACK;
		rbtree_rotate_right(rbtree, child_parent);
	}
	else
	{
		assert(sibling->right->color == RED);
		sibling->right->color = BLACK;
		rbtree_rotate_left(rbtree, child_parent);
	}
}

int
rbtree_find_less_equal(rbtree_type *rbtree, const void *key, rbnode_type **result)
{
	int r;
	rbnode_type *node;

	assert(result);

	/* Check if rbtree is NULL */
	if (!rbtree) {
		*result = NULL;
		return 0;
	}

	/* We start at root... */
	node = rbtree->root;

	*result = NULL;

	/* While there are children... */
	while (node != RBTREE_NULL) {
		r = rbtree->cmp(key, node->key);
		if (r == 0) {
			/* Exact match */
			*result = node;
			return 1;
		}
		if (r < 0) {
			node = node->left;
		} else {
			/* Temporary match */
			*result = node;
			node = node->right;
		}
	}
	return 0;
}

/*
 * Finds the first element in the red black tree
 *
 */
rbnode_type *
rbtree_first (rbtree_type *rbtree)
{
	rbnode_type *node;

	/* Check if rbtree is NULL */
	if (!rbtree) {
		return NULL;
	}

	for (node = rbtree->root; node->left != RBTREE_NULL; node = node->left);
	return node;
}

rbnode_type *
rbtree_last (rbtree_type *rbtree)
{
	rbnode_type *node;

	/* Check if rbtree is NULL */
	if (!rbtree) {
		return NULL;
	}

	for (node = rbtree->root; node->right != RBTREE_NULL; node = node->right);
	return node;
}

/*
 * Returns the next node...
 *
 */
rbnode_type *
rbtree_next (rbnode_type *node)
{
	rbnode_type *parent;

	if (node->right != RBTREE_NULL) {
		/* One right, then keep on going left... */
		for (node = node->right; node->left != RBTREE_NULL; node = node->left);
	} else {
		parent = node->parent;
		while (parent != RBTREE_NULL && node == parent->right) {
			node = parent;
			parent = parent->parent;
		}
		node = parent;
	}
	return node;
}

rbnode_type *
rbtree_previous(rbnode_type *node)
{
	rbnode_type *parent;

	if (node->left != RBTREE_NULL) {
		/* One left, then keep on going right... */
		for (node = node->left; node->right != RBTREE_NULL; node = node->right);
	} else {
		parent = node->parent;
		while (parent != RBTREE_NULL && node == parent->left) {
			node = parent;
			parent = parent->parent;
		}
		node = parent;
	}
	return node;
}
