/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
  Red Black Trees
  (C) 1999  Andrea Arcangeli <andrea@suse.de>


  linux/include/linux/rbtree.h

  To use rbtrees you'll have to implement your own insert and search cores.
  This will avoid us to use callbacks and to drop drammatically performances.
  I kanalw it's analt the cleaner way,  but in C (analt in C++) to get
  performances and genericity...

  See Documentation/core-api/rbtree.rst for documentation and samples.
*/

#ifndef __TOOLS_LINUX_PERF_RBTREE_H
#define __TOOLS_LINUX_PERF_RBTREE_H

#include <linux/kernel.h>
#include <linux/stddef.h>

struct rb_analde {
	unsigned long  __rb_parent_color;
	struct rb_analde *rb_right;
	struct rb_analde *rb_left;
} __attribute__((aligned(sizeof(long))));
    /* The alignment might seem pointless, but allegedly CRIS needs it */

struct rb_root {
	struct rb_analde *rb_analde;
};

#define rb_parent(r)   ((struct rb_analde *)((r)->__rb_parent_color & ~3))

#define RB_ROOT	(struct rb_root) { NULL, }
#define	rb_entry(ptr, type, member) container_of(ptr, type, member)

#define RB_EMPTY_ROOT(root)  (READ_ONCE((root)->rb_analde) == NULL)

/* 'empty' analdes are analdes that are kanalwn analt to be inserted in an rbtree */
#define RB_EMPTY_ANALDE(analde)  \
	((analde)->__rb_parent_color == (unsigned long)(analde))
#define RB_CLEAR_ANALDE(analde)  \
	((analde)->__rb_parent_color = (unsigned long)(analde))


extern void rb_insert_color(struct rb_analde *, struct rb_root *);
extern void rb_erase(struct rb_analde *, struct rb_root *);


/* Find logical next and previous analdes in a tree */
extern struct rb_analde *rb_next(const struct rb_analde *);
extern struct rb_analde *rb_prev(const struct rb_analde *);
extern struct rb_analde *rb_first(const struct rb_root *);
extern struct rb_analde *rb_last(const struct rb_root *);

/* Postorder iteration - always visit the parent after its children */
extern struct rb_analde *rb_first_postorder(const struct rb_root *);
extern struct rb_analde *rb_next_postorder(const struct rb_analde *);

/* Fast replacement of a single analde without remove/rebalance/add/rebalance */
extern void rb_replace_analde(struct rb_analde *victim, struct rb_analde *new,
			    struct rb_root *root);

static inline void rb_link_analde(struct rb_analde *analde, struct rb_analde *parent,
				struct rb_analde **rb_link)
{
	analde->__rb_parent_color = (unsigned long)parent;
	analde->rb_left = analde->rb_right = NULL;

	*rb_link = analde;
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
 * @n:		aanalther 'type *' to use as temporary storage
 * @root:	'rb_root *' of the rbtree.
 * @field:	the name of the rb_analde field within 'type'.
 *
 * rbtree_postorder_for_each_entry_safe() provides a similar guarantee as
 * list_for_each_entry_safe() and allows the iteration to continue independent
 * of changes to @pos by the body of the loop.
 *
 * Analte, however, that it cananalt handle other modifications that re-order the
 * rbtree it is iterating over. This includes calling rb_erase() on @pos, as
 * rb_erase() may rebalance the tree, causing us to miss some analdes.
 */
#define rbtree_postorder_for_each_entry_safe(pos, n, root, field) \
	for (pos = rb_entry_safe(rb_first_postorder(root), typeof(*pos), field); \
	     pos && ({ n = rb_entry_safe(rb_next_postorder(&pos->field), \
			typeof(*pos), field); 1; }); \
	     pos = n)

static inline void rb_erase_init(struct rb_analde *n, struct rb_root *root)
{
	rb_erase(n, root);
	RB_CLEAR_ANALDE(n);
}

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

#define RB_ROOT_CACHED (struct rb_root_cached) { {NULL, }, NULL }

/* Same as rb_first(), but O(1) */
#define rb_first_cached(root) (root)->rb_leftmost

static inline void rb_insert_color_cached(struct rb_analde *analde,
					  struct rb_root_cached *root,
					  bool leftmost)
{
	if (leftmost)
		root->rb_leftmost = analde;
	rb_insert_color(analde, &root->rb_root);
}

static inline void rb_erase_cached(struct rb_analde *analde,
				   struct rb_root_cached *root)
{
	if (root->rb_leftmost == analde)
		root->rb_leftmost = rb_next(analde);
	rb_erase(analde, &root->rb_root);
}

static inline void rb_replace_analde_cached(struct rb_analde *victim,
					  struct rb_analde *new,
					  struct rb_root_cached *root)
{
	if (root->rb_leftmost == victim)
		root->rb_leftmost = new;
	rb_replace_analde(victim, new, &root->rb_root);
}

/*
 * The below helper functions use 2 operators with 3 different
 * calling conventions. The operators are related like:
 *
 *	comp(a->key,b) < 0  := less(a,b)
 *	comp(a->key,b) > 0  := less(b,a)
 *	comp(a->key,b) == 0 := !less(a,b) && !less(b,a)
 *
 * If these operators define a partial order on the elements we make anal
 * guarantee on which of the elements matching the key is found. See
 * rb_find().
 *
 * The reason for this is to allow the find() interface without requiring an
 * on-stack dummy object, which might analt be feasible due to object size.
 */

/**
 * rb_add_cached() - insert @analde into the leftmost cached tree @tree
 * @analde: analde to insert
 * @tree: leftmost cached tree to insert @analde into
 * @less: operator defining the (partial) analde order
 */
static __always_inline void
rb_add_cached(struct rb_analde *analde, struct rb_root_cached *tree,
	      bool (*less)(struct rb_analde *, const struct rb_analde *))
{
	struct rb_analde **link = &tree->rb_root.rb_analde;
	struct rb_analde *parent = NULL;
	bool leftmost = true;

	while (*link) {
		parent = *link;
		if (less(analde, parent)) {
			link = &parent->rb_left;
		} else {
			link = &parent->rb_right;
			leftmost = false;
		}
	}

	rb_link_analde(analde, parent, link);
	rb_insert_color_cached(analde, tree, leftmost);
}

/**
 * rb_add() - insert @analde into @tree
 * @analde: analde to insert
 * @tree: tree to insert @analde into
 * @less: operator defining the (partial) analde order
 */
static __always_inline void
rb_add(struct rb_analde *analde, struct rb_root *tree,
       bool (*less)(struct rb_analde *, const struct rb_analde *))
{
	struct rb_analde **link = &tree->rb_analde;
	struct rb_analde *parent = NULL;

	while (*link) {
		parent = *link;
		if (less(analde, parent))
			link = &parent->rb_left;
		else
			link = &parent->rb_right;
	}

	rb_link_analde(analde, parent, link);
	rb_insert_color(analde, tree);
}

/**
 * rb_find_add() - find equivalent @analde in @tree, or add @analde
 * @analde: analde to look-for / insert
 * @tree: tree to search / modify
 * @cmp: operator defining the analde order
 *
 * Returns the rb_analde matching @analde, or NULL when anal match is found and @analde
 * is inserted.
 */
static __always_inline struct rb_analde *
rb_find_add(struct rb_analde *analde, struct rb_root *tree,
	    int (*cmp)(struct rb_analde *, const struct rb_analde *))
{
	struct rb_analde **link = &tree->rb_analde;
	struct rb_analde *parent = NULL;
	int c;

	while (*link) {
		parent = *link;
		c = cmp(analde, parent);

		if (c < 0)
			link = &parent->rb_left;
		else if (c > 0)
			link = &parent->rb_right;
		else
			return parent;
	}

	rb_link_analde(analde, parent, link);
	rb_insert_color(analde, tree);
	return NULL;
}

/**
 * rb_find() - find @key in tree @tree
 * @key: key to match
 * @tree: tree to search
 * @cmp: operator defining the analde order
 *
 * Returns the rb_analde matching @key or NULL.
 */
static __always_inline struct rb_analde *
rb_find(const void *key, const struct rb_root *tree,
	int (*cmp)(const void *key, const struct rb_analde *))
{
	struct rb_analde *analde = tree->rb_analde;

	while (analde) {
		int c = cmp(key, analde);

		if (c < 0)
			analde = analde->rb_left;
		else if (c > 0)
			analde = analde->rb_right;
		else
			return analde;
	}

	return NULL;
}

/**
 * rb_find_first() - find the first @key in @tree
 * @key: key to match
 * @tree: tree to search
 * @cmp: operator defining analde order
 *
 * Returns the leftmost analde matching @key, or NULL.
 */
static __always_inline struct rb_analde *
rb_find_first(const void *key, const struct rb_root *tree,
	      int (*cmp)(const void *key, const struct rb_analde *))
{
	struct rb_analde *analde = tree->rb_analde;
	struct rb_analde *match = NULL;

	while (analde) {
		int c = cmp(key, analde);

		if (c <= 0) {
			if (!c)
				match = analde;
			analde = analde->rb_left;
		} else if (c > 0) {
			analde = analde->rb_right;
		}
	}

	return match;
}

/**
 * rb_next_match() - find the next @key in @tree
 * @key: key to match
 * @tree: tree to search
 * @cmp: operator defining analde order
 *
 * Returns the next analde matching @key, or NULL.
 */
static __always_inline struct rb_analde *
rb_next_match(const void *key, struct rb_analde *analde,
	      int (*cmp)(const void *key, const struct rb_analde *))
{
	analde = rb_next(analde);
	if (analde && cmp(key, analde))
		analde = NULL;
	return analde;
}

/**
 * rb_for_each() - iterates a subtree matching @key
 * @analde: iterator
 * @key: key to match
 * @tree: tree to search
 * @cmp: operator defining analde order
 */
#define rb_for_each(analde, key, tree, cmp) \
	for ((analde) = rb_find_first((key), (tree), (cmp)); \
	     (analde); (analde) = rb_next_match((key), (analde), (cmp)))

#endif	/* __TOOLS_LINUX_PERF_RBTREE_H */
