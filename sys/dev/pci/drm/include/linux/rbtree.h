/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013, 2014 Mellanox Technologies, Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef	_LINUX_RBTREE_H_
#define	_LINUX_RBTREE_H_

#include <sys/tree.h>

struct rb_node {
	RB_ENTRY(rb_node)	__entry;
};
#define	rb_left		__entry.rbe_left
#define	rb_right	__entry.rbe_right

/*
 * We provide a false structure that has the same bit pattern as tree.h
 * presents so it matches the member names expected by linux.
 */
struct rb_root {
	struct	rb_node	*rb_node;
};

struct rb_root_cached {
	struct rb_root rb_root;
};

/*
 * In linux all of the comparisons are done by the caller.
 */
int panic_cmp(struct rb_node *one, struct rb_node *two);

RB_HEAD(linux_root, rb_node);
RB_PROTOTYPE(linux_root, rb_node, __entry, panic_cmp);

#define	rb_parent(r)	RB_PARENT(r, __entry)
#define	rb_color(r)	RB_COLOR(r, __entry)
#define	rb_is_red(r)	(rb_color(r) == RB_RED)
#define	rb_is_black(r)	(rb_color(r) == RB_BLACK)
#define	rb_set_parent(r, p)	rb_parent((r)) = (p)
#define	rb_set_color(r, c)	rb_color((r)) = (c)
#define	rb_entry(ptr, type, member)	container_of(ptr, type, member)
#define	rb_entry_safe(ptr, type, member) \
	(ptr ? rb_entry(ptr, type, member) : NULL)

#define RB_EMPTY_ROOT(root)	((root)->rb_node == NULL)
#define RB_EMPTY_NODE(node)     (rb_parent(node) == node)
#define RB_CLEAR_NODE(node)     (rb_set_parent(node, node))

#define	rb_insert_color(node, root)					\
	linux_root_RB_INSERT_COLOR((struct linux_root *)(root), (node))
#define	rb_erase(node, root)						\
	linux_root_RB_REMOVE((struct linux_root *)(root), (node))
#define	rb_next(node)	RB_NEXT(linux_root, NULL, (node))
#define	rb_prev(node)	RB_PREV(linux_root, NULL, (node))
#define	rb_first(root)	RB_MIN(linux_root, (struct linux_root *)(root))
#define	rb_last(root)	RB_MAX(linux_root, (struct linux_root *)(root))

#define	rb_insert_color_cached(node, root, leftmost)			\
	linux_root_RB_INSERT_COLOR((struct linux_root *)(&(root)->rb_root), (node))
#define	rb_erase_cached(node, root)						\
	linux_root_RB_REMOVE((struct linux_root *)(&(root)->rb_root), (node))
#define	rb_first_cached(root)	RB_MIN(linux_root, (struct linux_root *)(&(root)->rb_root))
#define	rb_replace_node_cached(old, new, root)				\
	rb_replace_node(old, new, &(root)->rb_root)

static inline struct rb_node *
__rb_deepest_left(struct rb_node *node)
{
	struct rb_node *parent = NULL;
	while (node) {
		parent = node;
		if (RB_LEFT(node, __entry))
			node = RB_LEFT(node, __entry);
		else
			node = RB_RIGHT(node, __entry);
	}
	return parent;	
}

static inline struct rb_node *
rb_next_postorder(const struct rb_node *node)
{
	struct rb_node *parent = RB_PARENT(node, __entry);
	/* left -> right, right -> root */
	if (parent != NULL &&
	    (node == RB_LEFT(parent, __entry)) &&
	    (RB_RIGHT(parent, __entry)))
		return __rb_deepest_left(RB_RIGHT(parent, __entry));
	else
		return parent;
}

#define	rbtree_postorder_for_each_entry_safe(x, y, head, member)	\
	for ((x) = rb_entry_safe(__rb_deepest_left((head)->rb_node),	\
	    __typeof(*x), member);					\
	    ((x) != NULL) && ((y) =					\
	    rb_entry_safe(rb_next_postorder(&x->member), typeof(*x), member), 1); \
	    (x) = (y))

static inline void
rb_link_node(struct rb_node *node, struct rb_node *parent,
    struct rb_node **rb_link)
{
	rb_set_parent(node, parent);
	rb_set_color(node, RB_RED);
	node->__entry.rbe_left = node->__entry.rbe_right = NULL;
	*rb_link = node;
}

static inline void
rb_replace_node(struct rb_node *victim, struct rb_node *new,
    struct rb_root *root)
{
	struct rb_node *p;

	p = rb_parent(victim);
	if (p) {
		if (p->rb_left == victim)
			p->rb_left = new;
		else
			p->rb_right = new;
	} else
		root->rb_node = new;
	if (victim->rb_left)
		rb_set_parent(victim->rb_left, new);
	if (victim->rb_right)
		rb_set_parent(victim->rb_right, new);
	*new = *victim;
}

static inline void
rb_add_cached(struct rb_node *node, struct rb_root_cached *root,
    bool (*less)(struct rb_node *, const struct rb_node *))
{
	struct rb_node **iter = &root->rb_root.rb_node;
	struct rb_node *parent = NULL;

	while (*iter) {
		parent = *iter;

		if (less(node, parent))
			iter = &(*iter)->rb_left;
		else
			iter = &(*iter)->rb_right;
	}

	rb_link_node(node, parent, iter);
	rb_insert_color_cached(node, root, false);
}

#undef RB_ROOT
#define RB_ROOT		(struct rb_root) { NULL }
#ifdef __clang__
#define RB_ROOT_CACHED	(struct rb_root_cached) { NULL }
#else
#define RB_ROOT_CACHED	(struct rb_root_cached) { { NULL } }
#endif

#endif	/* _LINUX_RBTREE_H_ */
