#ifndef __TOOLS_LINUX_PERF_RBTREE_H
#define __TOOLS_LINUX_PERF_RBTREE_H
#include <stdbool.h>
#include "../../../../include/linux/rbtree.h"

/*
 * Handy for checking that we are not deleting an entry that is
 * already in a list, found in block/{blk-throttle,cfq-iosched}.c,
 * probably should be moved to lib/rbtree.c...
 */
static inline void rb_erase_init(struct rb_node *n, struct rb_root *root)
{
	rb_erase(n, root);
	RB_CLEAR_NODE(n);
}
#endif /* __TOOLS_LINUX_PERF_RBTREE_H */
