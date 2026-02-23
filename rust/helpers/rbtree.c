// SPDX-License-Identifier: GPL-2.0

#include <linux/rbtree.h>

__rust_helper void rust_helper_rb_link_node(struct rb_node *node,
					    struct rb_node *parent,
					    struct rb_node **rb_link)
{
	rb_link_node(node, parent, rb_link);
}

__rust_helper struct rb_node *rust_helper_rb_first(const struct rb_root *root)
{
	return rb_first(root);
}

__rust_helper struct rb_node *rust_helper_rb_last(const struct rb_root *root)
{
	return rb_last(root);
}
