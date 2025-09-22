/* Public domain. */

#ifndef _LINUX_INTERVAL_TREE_H
#define _LINUX_INTERVAL_TREE_H

#include <linux/rbtree.h>

struct interval_tree_node {
	struct rb_node rb;
	unsigned long start;
	unsigned long last;
};

struct interval_tree_node *interval_tree_iter_first(struct rb_root_cached *,
    unsigned long, unsigned long);
void interval_tree_insert(struct interval_tree_node *, struct rb_root_cached *);
void interval_tree_remove(struct interval_tree_node *, struct rb_root_cached *);

#endif
