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

struct callchain_node {
	struct callchain_node	*parent;
	struct list_head	brothers;
	struct list_head	children;
	struct list_head	val;
	struct rb_node		rb_node; /* to sort nodes in an rbtree */
	struct rb_root		rb_root; /* sorted tree of children */
	unsigned int		val_nr;
	u64			hit;
	u64			children_hit;
};

struct callchain_param;

typedef void (*sort_chain_func_t)(struct rb_root *, struct callchain_node *,
				 u64, struct callchain_param *);

struct callchain_param {
	enum chain_mode 	mode;
	u32			print_limit;
	double			min_percent;
	sort_chain_func_t	sort;
};

struct callchain_list {
	u64			ip;
	struct map_symbol	ms;
	struct list_head	list;
};

static inline void callchain_init(struct callchain_node *node)
{
	INIT_LIST_HEAD(&node->brothers);
	INIT_LIST_HEAD(&node->children);
	INIT_LIST_HEAD(&node->val);

	node->children_hit = 0;
	node->parent = NULL;
	node->hit = 0;
}

static inline u64 cumul_hits(struct callchain_node *node)
{
	return node->hit + node->children_hit;
}

int register_callchain_param(struct callchain_param *param);
int append_chain(struct callchain_node *root, struct ip_callchain *chain,
		 struct map_symbol *syms, u64 period);

bool ip_callchain__valid(struct ip_callchain *chain, const event_t *event);
#endif	/* __PERF_CALLCHAIN_H */
