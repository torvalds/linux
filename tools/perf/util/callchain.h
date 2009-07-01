#ifndef __PERF_CALLCHAIN_H
#define __PERF_CALLCHAIN_H

#include "../perf.h"
#include "list.h"
#include <linux/rbtree.h>
#include "symbol.h"


struct callchain_node {
	struct callchain_node	*parent;
	struct list_head	brothers;
	struct list_head	children;
	struct list_head	val;
	struct rb_node		rb_node;
	unsigned int		val_nr;
	u64			hit;
};

struct callchain_list {
	u64			ip;
	struct symbol		*sym;
	struct list_head	list;
};

static inline void callchain_init(struct callchain_node *node)
{
	INIT_LIST_HEAD(&node->brothers);
	INIT_LIST_HEAD(&node->children);
	INIT_LIST_HEAD(&node->val);
}

void append_chain(struct callchain_node *root, struct ip_callchain *chain,
		  struct symbol **syms);
void sort_chain_to_rbtree(struct rb_root *rb_root, struct callchain_node *node);
#endif
