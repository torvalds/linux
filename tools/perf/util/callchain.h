#ifndef __PERF_CALLCHAIN_H
#define __PERF_CALLCHAIN_H

#include "../perf.h"
#include <linux/list.h>
#include <linux/rbtree.h>
#include "event.h"
#include "symbol.h"

enum chain_mode {
	CHAIN_NONE,
	CHAIN_FLAT,
	CHAIN_GRAPH_ABS,
	CHAIN_GRAPH_REL
};

enum chain_order {
	ORDER_CALLER,
	ORDER_CALLEE
};

struct callchain_node {
	struct callchain_node	*parent;
	struct list_head	siblings;
	struct list_head	children;
	struct list_head	val;
	struct rb_node		rb_node; /* to sort nodes in an rbtree */
	struct rb_root		rb_root; /* sorted tree of children */
	unsigned int		val_nr;
	u64			hit;
	u64			children_hit;
};

struct callchain_root {
	u64			max_depth;
	struct callchain_node	node;
};

struct callchain_param;

typedef void (*sort_chain_func_t)(struct rb_root *, struct callchain_root *,
				 u64, struct callchain_param *);

struct callchain_param {
	enum chain_mode 	mode;
	u32			print_limit;
	double			min_percent;
	sort_chain_func_t	sort;
	enum chain_order	order;
};

struct callchain_list {
	u64			ip;
	struct map_symbol	ms;
	struct list_head	list;
};

/*
 * A callchain cursor is a single linked list that
 * let one feed a callchain progressively.
 * It keeps persitent allocated entries to minimize
 * allocations.
 */
struct callchain_cursor_node {
	u64				ip;
	struct map			*map;
	struct symbol			*sym;
	struct callchain_cursor_node	*next;
};

struct callchain_cursor {
	u64				nr;
	struct callchain_cursor_node	*first;
	struct callchain_cursor_node	**last;
	u64				pos;
	struct callchain_cursor_node	*curr;
};

static inline void callchain_init(struct callchain_root *root)
{
	INIT_LIST_HEAD(&root->node.siblings);
	INIT_LIST_HEAD(&root->node.children);
	INIT_LIST_HEAD(&root->node.val);

	root->node.parent = NULL;
	root->node.hit = 0;
	root->node.children_hit = 0;
	root->max_depth = 0;
}

static inline u64 callchain_cumul_hits(struct callchain_node *node)
{
	return node->hit + node->children_hit;
}

int callchain_register_param(struct callchain_param *param);
int callchain_append(struct callchain_root *root,
		     struct callchain_cursor *cursor,
		     u64 period);

int callchain_merge(struct callchain_cursor *cursor,
		    struct callchain_root *dst, struct callchain_root *src);

bool ip_callchain__valid(struct ip_callchain *chain,
			 const union perf_event *event);
/*
 * Initialize a cursor before adding entries inside, but keep
 * the previously allocated entries as a cache.
 */
static inline void callchain_cursor_reset(struct callchain_cursor *cursor)
{
	cursor->nr = 0;
	cursor->last = &cursor->first;
}

int callchain_cursor_append(struct callchain_cursor *cursor, u64 ip,
			    struct map *map, struct symbol *sym);

/* Close a cursor writing session. Initialize for the reader */
static inline void callchain_cursor_commit(struct callchain_cursor *cursor)
{
	cursor->curr = cursor->first;
	cursor->pos = 0;
}

/* Cursor reading iteration helpers */
static inline struct callchain_cursor_node *
callchain_cursor_current(struct callchain_cursor *cursor)
{
	if (cursor->pos == cursor->nr)
		return NULL;

	return cursor->curr;
}

static inline void callchain_cursor_advance(struct callchain_cursor *cursor)
{
	cursor->curr = cursor->curr->next;
	cursor->pos++;
}
#endif	/* __PERF_CALLCHAIN_H */
