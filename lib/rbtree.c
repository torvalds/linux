/*
  Red Black Trees
  (C) 1999  Andrea Arcangeli <andrea@suse.de>
  (C) 2002  David Woodhouse <dwmw2@infradead.org>
  
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

  linux/lib/rbtree.c
*/

#include <linux/rbtree.h>
#include <linux/module.h>

static void __rb_rotate_left(struct rb_node *node, struct rb_root *root)
{
	struct rb_node *right = node->rb_right;

	if ((node->rb_right = right->rb_left))
		right->rb_left->rb_parent = node;
	right->rb_left = node;

	if ((right->rb_parent = node->rb_parent))
	{
		if (node == node->rb_parent->rb_left)
			node->rb_parent->rb_left = right;
		else
			node->rb_parent->rb_right = right;
	}
	else
		root->rb_node = right;
	node->rb_parent = right;
}

static void __rb_rotate_right(struct rb_node *node, struct rb_root *root)
{
	struct rb_node *left = node->rb_left;

	if ((node->rb_left = left->rb_right))
		left->rb_right->rb_parent = node;
	left->rb_right = node;

	if ((left->rb_parent = node->rb_parent))
	{
		if (node == node->rb_parent->rb_right)
			node->rb_parent->rb_right = left;
		else
			node->rb_parent->rb_left = left;
	}
	else
		root->rb_node = left;
	node->rb_parent = left;
}

void rb_insert_color(struct rb_node *node, struct rb_root *root)
{
	struct rb_node *parent, *gparent;

	while ((parent = node->rb_parent) && parent->rb_color == RB_RED)
	{
		gparent = parent->rb_parent;

		if (parent == gparent->rb_left)
		{
			{
				register struct rb_node *uncle = gparent->rb_right;
				if (uncle && uncle->rb_color == RB_RED)
				{
					uncle->rb_color = RB_BLACK;
					parent->rb_color = RB_BLACK;
					gparent->rb_color = RB_RED;
					node = gparent;
					continue;
				}
			}

			if (parent->rb_right == node)
			{
				register struct rb_node *tmp;
				__rb_rotate_left(parent, root);
				tmp = parent;
				parent = node;
				node = tmp;
			}

			parent->rb_color = RB_BLACK;
			gparent->rb_color = RB_RED;
			__rb_rotate_right(gparent, root);
		} else {
			{
				register struct rb_node *uncle = gparent->rb_left;
				if (uncle && uncle->rb_color == RB_RED)
				{
					uncle->rb_color = RB_BLACK;
					parent->rb_color = RB_BLACK;
					gparent->rb_color = RB_RED;
					node = gparent;
					continue;
				}
			}

			if (parent->rb_left == node)
			{
				register struct rb_node *tmp;
				__rb_rotate_right(parent, root);
				tmp = parent;
				parent = node;
				node = tmp;
			}

			parent->rb_color = RB_BLACK;
			gparent->rb_color = RB_RED;
			__rb_rotate_left(gparent, root);
		}
	}

	root->rb_node->rb_color = RB_BLACK;
}
EXPORT_SYMBOL(rb_insert_color);

static void __rb_erase_color(struct rb_node *node, struct rb_node *parent,
			     struct rb_root *root)
{
	struct rb_node *other;

	while ((!node || node->rb_color == RB_BLACK) && node != root->rb_node)
	{
		if (parent->rb_left == node)
		{
			other = parent->rb_right;
			if (other->rb_color == RB_RED)
			{
				other->rb_color = RB_BLACK;
				parent->rb_color = RB_RED;
				__rb_rotate_left(parent, root);
				other = parent->rb_right;
			}
			if ((!other->rb_left ||
			     other->rb_left->rb_color == RB_BLACK)
			    && (!other->rb_right ||
				other->rb_right->rb_color == RB_BLACK))
			{
				other->rb_color = RB_RED;
				node = parent;
				parent = node->rb_parent;
			}
			else
			{
				if (!other->rb_right ||
				    other->rb_right->rb_color == RB_BLACK)
				{
					register struct rb_node *o_left;
					if ((o_left = other->rb_left))
						o_left->rb_color = RB_BLACK;
					other->rb_color = RB_RED;
					__rb_rotate_right(other, root);
					other = parent->rb_right;
				}
				other->rb_color = parent->rb_color;
				parent->rb_color = RB_BLACK;
				if (other->rb_right)
					other->rb_right->rb_color = RB_BLACK;
				__rb_rotate_left(parent, root);
				node = root->rb_node;
				break;
			}
		}
		else
		{
			other = parent->rb_left;
			if (other->rb_color == RB_RED)
			{
				other->rb_color = RB_BLACK;
				parent->rb_color = RB_RED;
				__rb_rotate_right(parent, root);
				other = parent->rb_left;
			}
			if ((!other->rb_left ||
			     other->rb_left->rb_color == RB_BLACK)
			    && (!other->rb_right ||
				other->rb_right->rb_color == RB_BLACK))
			{
				other->rb_color = RB_RED;
				node = parent;
				parent = node->rb_parent;
			}
			else
			{
				if (!other->rb_left ||
				    other->rb_left->rb_color == RB_BLACK)
				{
					register struct rb_node *o_right;
					if ((o_right = other->rb_right))
						o_right->rb_color = RB_BLACK;
					other->rb_color = RB_RED;
					__rb_rotate_left(other, root);
					other = parent->rb_left;
				}
				other->rb_color = parent->rb_color;
				parent->rb_color = RB_BLACK;
				if (other->rb_left)
					other->rb_left->rb_color = RB_BLACK;
				__rb_rotate_right(parent, root);
				node = root->rb_node;
				break;
			}
		}
	}
	if (node)
		node->rb_color = RB_BLACK;
}

void rb_erase(struct rb_node *node, struct rb_root *root)
{
	struct rb_node *child, *parent;
	int color;

	if (!node->rb_left)
		child = node->rb_right;
	else if (!node->rb_right)
		child = node->rb_left;
	else
	{
		struct rb_node *old = node, *left;

		node = node->rb_right;
		while ((left = node->rb_left) != NULL)
			node = left;
		child = node->rb_right;
		parent = node->rb_parent;
		color = node->rb_color;

		if (child)
			child->rb_parent = parent;
		if (parent)
		{
			if (parent->rb_left == node)
				parent->rb_left = child;
			else
				parent->rb_right = child;
		}
		else
			root->rb_node = child;

		if (node->rb_parent == old)
			parent = node;
		node->rb_parent = old->rb_parent;
		node->rb_color = old->rb_color;
		node->rb_right = old->rb_right;
		node->rb_left = old->rb_left;

		if (old->rb_parent)
		{
			if (old->rb_parent->rb_left == old)
				old->rb_parent->rb_left = node;
			else
				old->rb_parent->rb_right = node;
		} else
			root->rb_node = node;

		old->rb_left->rb_parent = node;
		if (old->rb_right)
			old->rb_right->rb_parent = node;
		goto color;
	}

	parent = node->rb_parent;
	color = node->rb_color;

	if (child)
		child->rb_parent = parent;
	if (parent)
	{
		if (parent->rb_left == node)
			parent->rb_left = child;
		else
			parent->rb_right = child;
	}
	else
		root->rb_node = child;

 color:
	if (color == RB_BLACK)
		__rb_erase_color(child, parent, root);
}
EXPORT_SYMBOL(rb_erase);

/*
 * This function returns the first node (in sort order) of the tree.
 */
struct rb_node *rb_first(struct rb_root *root)
{
	struct rb_node	*n;

	n = root->rb_node;
	if (!n)
		return NULL;
	while (n->rb_left)
		n = n->rb_left;
	return n;
}
EXPORT_SYMBOL(rb_first);

struct rb_node *rb_last(struct rb_root *root)
{
	struct rb_node	*n;

	n = root->rb_node;
	if (!n)
		return NULL;
	while (n->rb_right)
		n = n->rb_right;
	return n;
}
EXPORT_SYMBOL(rb_last);

struct rb_node *rb_next(struct rb_node *node)
{
	/* If we have a right-hand child, go down and then left as far
	   as we can. */
	if (node->rb_right) {
		node = node->rb_right; 
		while (node->rb_left)
			node=node->rb_left;
		return node;
	}

	/* No right-hand children.  Everything down and left is
	   smaller than us, so any 'next' node must be in the general
	   direction of our parent. Go up the tree; any time the
	   ancestor is a right-hand child of its parent, keep going
	   up. First time it's a left-hand child of its parent, said
	   parent is our 'next' node. */
	while (node->rb_parent && node == node->rb_parent->rb_right)
		node = node->rb_parent;

	return node->rb_parent;
}
EXPORT_SYMBOL(rb_next);

struct rb_node *rb_prev(struct rb_node *node)
{
	/* If we have a left-hand child, go down and then right as far
	   as we can. */
	if (node->rb_left) {
		node = node->rb_left; 
		while (node->rb_right)
			node=node->rb_right;
		return node;
	}

	/* No left-hand children. Go up till we find an ancestor which
	   is a right-hand child of its parent */
	while (node->rb_parent && node == node->rb_parent->rb_left)
		node = node->rb_parent;

	return node->rb_parent;
}
EXPORT_SYMBOL(rb_prev);

void rb_replace_node(struct rb_node *victim, struct rb_node *new,
		     struct rb_root *root)
{
	struct rb_node *parent = victim->rb_parent;

	/* Set the surrounding nodes to point to the replacement */
	if (parent) {
		if (victim == parent->rb_left)
			parent->rb_left = new;
		else
			parent->rb_right = new;
	} else {
		root->rb_node = new;
	}
	if (victim->rb_left)
		victim->rb_left->rb_parent = new;
	if (victim->rb_right)
		victim->rb_right->rb_parent = new;

	/* Copy the pointers/colour from the victim to the replacement */
	*new = *victim;
}
EXPORT_SYMBOL(rb_replace_node);
