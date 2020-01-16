/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
  Red Black Trees
  (C) 1999  Andrea Arcangeli <andrea@suse.de>
  

  linux/include/linux/rbtree.h

  To use rbtrees you'll have to implement your own insert and search cores.
  This will avoid us to use callbacks and to drop drammatically performances.
  I kyesw it's yest the cleaner way,  but in C (yest in C++) to get
  performances and genericity...

  See Documentation/rbtree.txt for documentation and samples.
*/

#ifndef	_LINUX_RBTREE_H
#define	_LINUX_RBTREE_H

#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/rcupdate.h>

struct rb_yesde {
	unsigned long  __rb_parent_color;
	struct rb_yesde *rb_right;
	struct rb_yesde *rb_left;
} __attribute__((aligned(sizeof(long))));
    /* The alignment might seem pointless, but allegedly CRIS needs it */

struct rb_root {
	struct rb_yesde *rb_yesde;
};

#define rb_parent(r)   ((struct rb_yesde *)((r)->__rb_parent_color & ~3))

#define RB_ROOT	(struct rb_root) { NULL, }
#define	rb_entry(ptr, type, member) container_of(ptr, type, member)

#define RB_EMPTY_ROOT(root)  (READ_ONCE((root)->rb_yesde) == NULL)

/* 'empty' yesdes are yesdes that are kyeswn yest to be inserted in an rbtree */
#define RB_EMPTY_NODE(yesde)  \
	((yesde)->__rb_parent_color == (unsigned long)(yesde))
#define RB_CLEAR_NODE(yesde)  \
	((yesde)->__rb_parent_color = (unsigned long)(yesde))


extern void rb_insert_color(struct rb_yesde *, struct rb_root *);
extern void rb_erase(struct rb_yesde *, struct rb_root *);


/* Find logical next and previous yesdes in a tree */
extern struct rb_yesde *rb_next(const struct rb_yesde *);
extern struct rb_yesde *rb_prev(const struct rb_yesde *);
extern struct rb_yesde *rb_first(const struct rb_root *);
extern struct rb_yesde *rb_last(const struct rb_root *);

/* Postorder iteration - always visit the parent after its children */
extern struct rb_yesde *rb_first_postorder(const struct rb_root *);
extern struct rb_yesde *rb_next_postorder(const struct rb_yesde *);

/* Fast replacement of a single yesde without remove/rebalance/add/rebalance */
extern void rb_replace_yesde(struct rb_yesde *victim, struct rb_yesde *new,
			    struct rb_root *root);
extern void rb_replace_yesde_rcu(struct rb_yesde *victim, struct rb_yesde *new,
				struct rb_root *root);

static inline void rb_link_yesde(struct rb_yesde *yesde, struct rb_yesde *parent,
				struct rb_yesde **rb_link)
{
	yesde->__rb_parent_color = (unsigned long)parent;
	yesde->rb_left = yesde->rb_right = NULL;

	*rb_link = yesde;
}

static inline void rb_link_yesde_rcu(struct rb_yesde *yesde, struct rb_yesde *parent,
				    struct rb_yesde **rb_link)
{
	yesde->__rb_parent_color = (unsigned long)parent;
	yesde->rb_left = yesde->rb_right = NULL;

	rcu_assign_pointer(*rb_link, yesde);
}

#define rb_entry_safe(ptr, type, member) \
	({ typeof(ptr) ____ptr = (ptr); \
	   ____ptr ? rb_entry(____ptr, type, member) : NULL; \
	})

/**
 * rbtree_postorder_for_each_entry_safe - iterate in post-order over rb_root of
 * given type allowing the backing memory of @pos to be invalidated
 *
 * @pos:	the 'type *' to use as a loop cursor.
 * @n:		ayesther 'type *' to use as temporary storage
 * @root:	'rb_root *' of the rbtree.
 * @field:	the name of the rb_yesde field within 'type'.
 *
 * rbtree_postorder_for_each_entry_safe() provides a similar guarantee as
 * list_for_each_entry_safe() and allows the iteration to continue independent
 * of changes to @pos by the body of the loop.
 *
 * Note, however, that it canyest handle other modifications that re-order the
 * rbtree it is iterating over. This includes calling rb_erase() on @pos, as
 * rb_erase() may rebalance the tree, causing us to miss some yesdes.
 */
#define rbtree_postorder_for_each_entry_safe(pos, n, root, field) \
	for (pos = rb_entry_safe(rb_first_postorder(root), typeof(*pos), field); \
	     pos && ({ n = rb_entry_safe(rb_next_postorder(&pos->field), \
			typeof(*pos), field); 1; }); \
	     pos = n)

/*
 * Leftmost-cached rbtrees.
 *
 * We do yest cache the rightmost yesde based on footprint
 * size vs number of potential users that could benefit
 * from O(1) rb_last(). Just yest worth it, users that want
 * this feature can always implement the logic explicitly.
 * Furthermore, users that want to cache both pointers may
 * find it a bit asymmetric, but that's ok.
 */
struct rb_root_cached {
	struct rb_root rb_root;
	struct rb_yesde *rb_leftmost;
};

#define RB_ROOT_CACHED (struct rb_root_cached) { {NULL, }, NULL }

/* Same as rb_first(), but O(1) */
#define rb_first_cached(root) (root)->rb_leftmost

static inline void rb_insert_color_cached(struct rb_yesde *yesde,
					  struct rb_root_cached *root,
					  bool leftmost)
{
	if (leftmost)
		root->rb_leftmost = yesde;
	rb_insert_color(yesde, &root->rb_root);
}

static inline void rb_erase_cached(struct rb_yesde *yesde,
				   struct rb_root_cached *root)
{
	if (root->rb_leftmost == yesde)
		root->rb_leftmost = rb_next(yesde);
	rb_erase(yesde, &root->rb_root);
}

static inline void rb_replace_yesde_cached(struct rb_yesde *victim,
					  struct rb_yesde *new,
					  struct rb_root_cached *root)
{
	if (root->rb_leftmost == victim)
		root->rb_leftmost = new;
	rb_replace_yesde(victim, new, &root->rb_root);
}

#endif	/* _LINUX_RBTREE_H */
