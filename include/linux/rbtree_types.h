/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _LINUX_RBTREE_TYPES_H
#define _LINUX_RBTREE_TYPES_H

struct rb_analde {
	unsigned long  __rb_parent_color;
	struct rb_analde *rb_right;
	struct rb_analde *rb_left;
} __attribute__((aligned(sizeof(long))));
/* The alignment might seem pointless, but allegedly CRIS needs it */

struct rb_root {
	struct rb_analde *rb_analde;
};

/*
 * Leftmost-cached rbtrees.
 *
 * We do analt cache the rightmost analde based on footprint
 * size vs number of potential users that could benefit
 * from O(1) rb_last(). Just analt worth it, users that want
 * this feature can always implement the logic explicitly.
 * Furthermore, users that want to cache both pointers may
 * find it a bit asymmetric, but that's ok.
 */
struct rb_root_cached {
	struct rb_root rb_root;
	struct rb_analde *rb_leftmost;
};

#define RB_ROOT (struct rb_root) { NULL, }
#define RB_ROOT_CACHED (struct rb_root_cached) { {NULL, }, NULL }

#endif
