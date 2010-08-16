/*
 * Copyright (C) 2009-2010, Frederic Weisbecker <fweisbec@gmail.com>
 *
 * Handle the callchains from the stream in an ad-hoc radix tree and then
 * sort them in an rbtree.
 *
 * Using a radix for code path provides a fast retrieval and factorizes
 * memory use. Also that lets us use the paths in a hierarchical graph view.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <math.h>

#include "util.h"
#include "callchain.h"

bool ip_callchain__valid(struct ip_callchain *chain, const event_t *event)
{
	unsigned int chain_size = event->header.size;
	chain_size -= (unsigned long)&event->ip.__more_data - (unsigned long)event;
	return chain->nr * sizeof(u64) <= chain_size;
}

#define chain_for_each_child(child, parent)	\
	list_for_each_entry(child, &parent->children, brothers)

static void
rb_insert_callchain(struct rb_root *root, struct callchain_node *chain,
		    enum chain_mode mode)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct callchain_node *rnode;
	u64 chain_cumul = cumul_hits(chain);

	while (*p) {
		u64 rnode_cumul;

		parent = *p;
		rnode = rb_entry(parent, struct callchain_node, rb_node);
		rnode_cumul = cumul_hits(rnode);

		switch (mode) {
		case CHAIN_FLAT:
			if (rnode->hit < chain->hit)
				p = &(*p)->rb_left;
			else
				p = &(*p)->rb_right;
			break;
		case CHAIN_GRAPH_ABS: /* Falldown */
		case CHAIN_GRAPH_REL:
			if (rnode_cumul < chain_cumul)
				p = &(*p)->rb_left;
			else
				p = &(*p)->rb_right;
			break;
		case CHAIN_NONE:
		default:
			break;
		}
	}

	rb_link_node(&chain->rb_node, parent, p);
	rb_insert_color(&chain->rb_node, root);
}

static void
__sort_chain_flat(struct rb_root *rb_root, struct callchain_node *node,
		  u64 min_hit)
{
	struct callchain_node *child;

	chain_for_each_child(child, node)
		__sort_chain_flat(rb_root, child, min_hit);

	if (node->hit && node->hit >= min_hit)
		rb_insert_callchain(rb_root, node, CHAIN_FLAT);
}

/*
 * Once we get every callchains from the stream, we can now
 * sort them by hit
 */
static void
sort_chain_flat(struct rb_root *rb_root, struct callchain_node *node,
		u64 min_hit, struct callchain_param *param __used)
{
	__sort_chain_flat(rb_root, node, min_hit);
}

static void __sort_chain_graph_abs(struct callchain_node *node,
				   u64 min_hit)
{
	struct callchain_node *child;

	node->rb_root = RB_ROOT;

	chain_for_each_child(child, node) {
		__sort_chain_graph_abs(child, min_hit);
		if (cumul_hits(child) >= min_hit)
			rb_insert_callchain(&node->rb_root, child,
					    CHAIN_GRAPH_ABS);
	}
}

static void
sort_chain_graph_abs(struct rb_root *rb_root, struct callchain_node *chain_root,
		     u64 min_hit, struct callchain_param *param __used)
{
	__sort_chain_graph_abs(chain_root, min_hit);
	rb_root->rb_node = chain_root->rb_root.rb_node;
}

static void __sort_chain_graph_rel(struct callchain_node *node,
				   double min_percent)
{
	struct callchain_node *child;
	u64 min_hit;

	node->rb_root = RB_ROOT;
	min_hit = ceil(node->children_hit * min_percent);

	chain_for_each_child(child, node) {
		__sort_chain_graph_rel(child, min_percent);
		if (cumul_hits(child) >= min_hit)
			rb_insert_callchain(&node->rb_root, child,
					    CHAIN_GRAPH_REL);
	}
}

static void
sort_chain_graph_rel(struct rb_root *rb_root, struct callchain_node *chain_root,
		     u64 min_hit __used, struct callchain_param *param)
{
	__sort_chain_graph_rel(chain_root, param->min_percent / 100.0);
	rb_root->rb_node = chain_root->rb_root.rb_node;
}

int register_callchain_param(struct callchain_param *param)
{
	switch (param->mode) {
	case CHAIN_GRAPH_ABS:
		param->sort = sort_chain_graph_abs;
		break;
	case CHAIN_GRAPH_REL:
		param->sort = sort_chain_graph_rel;
		break;
	case CHAIN_FLAT:
		param->sort = sort_chain_flat;
		break;
	case CHAIN_NONE:
	default:
		return -1;
	}
	return 0;
}

/*
 * Create a child for a parent. If inherit_children, then the new child
 * will become the new parent of it's parent children
 */
static struct callchain_node *
create_child(struct callchain_node *parent, bool inherit_children)
{
	struct callchain_node *new;

	new = zalloc(sizeof(*new));
	if (!new) {
		perror("not enough memory to create child for code path tree");
		return NULL;
	}
	new->parent = parent;
	INIT_LIST_HEAD(&new->children);
	INIT_LIST_HEAD(&new->val);

	if (inherit_children) {
		struct callchain_node *next;

		list_splice(&parent->children, &new->children);
		INIT_LIST_HEAD(&parent->children);

		chain_for_each_child(next, new)
			next->parent = new;
	}
	list_add_tail(&new->brothers, &parent->children);

	return new;
}


struct resolved_ip {
	u64		  ip;
	struct map_symbol ms;
};

struct resolved_chain {
	u64			nr;
	struct resolved_ip	ips[0];
};


/*
 * Fill the node with callchain values
 */
static void
fill_node(struct callchain_node *node, struct resolved_chain *chain, int start)
{
	unsigned int i;

	for (i = start; i < chain->nr; i++) {
		struct callchain_list *call;

		call = zalloc(sizeof(*call));
		if (!call) {
			perror("not enough memory for the code path tree");
			return;
		}
		call->ip = chain->ips[i].ip;
		call->ms = chain->ips[i].ms;
		list_add_tail(&call->list, &node->val);
	}
	node->val_nr = chain->nr - start;
	if (!node->val_nr)
		pr_warning("Warning: empty node in callchain tree\n");
}

static void
add_child(struct callchain_node *parent, struct resolved_chain *chain,
	  int start, u64 period)
{
	struct callchain_node *new;

	new = create_child(parent, false);
	fill_node(new, chain, start);

	new->children_hit = 0;
	new->hit = period;
}

/*
 * Split the parent in two parts (a new child is created) and
 * give a part of its callchain to the created child.
 * Then create another child to host the given callchain of new branch
 */
static void
split_add_child(struct callchain_node *parent, struct resolved_chain *chain,
		struct callchain_list *to_split, int idx_parents, int idx_local,
		u64 period)
{
	struct callchain_node *new;
	struct list_head *old_tail;
	unsigned int idx_total = idx_parents + idx_local;

	/* split */
	new = create_child(parent, true);

	/* split the callchain and move a part to the new child */
	old_tail = parent->val.prev;
	list_del_range(&to_split->list, old_tail);
	new->val.next = &to_split->list;
	new->val.prev = old_tail;
	to_split->list.prev = &new->val;
	old_tail->next = &new->val;

	/* split the hits */
	new->hit = parent->hit;
	new->children_hit = parent->children_hit;
	parent->children_hit = cumul_hits(new);
	new->val_nr = parent->val_nr - idx_local;
	parent->val_nr = idx_local;

	/* create a new child for the new branch if any */
	if (idx_total < chain->nr) {
		parent->hit = 0;
		add_child(parent, chain, idx_total, period);
		parent->children_hit += period;
	} else {
		parent->hit = period;
	}
}

static int
__append_chain(struct callchain_node *root, struct resolved_chain *chain,
	       unsigned int start, u64 period);

static void
__append_chain_children(struct callchain_node *root,
			struct resolved_chain *chain,
			unsigned int start, u64 period)
{
	struct callchain_node *rnode;

	/* lookup in childrens */
	chain_for_each_child(rnode, root) {
		unsigned int ret = __append_chain(rnode, chain, start, period);

		if (!ret)
			goto inc_children_hit;
	}
	/* nothing in children, add to the current node */
	add_child(root, chain, start, period);

inc_children_hit:
	root->children_hit += period;
}

static int
__append_chain(struct callchain_node *root, struct resolved_chain *chain,
	       unsigned int start, u64 period)
{
	struct callchain_list *cnode;
	unsigned int i = start;
	bool found = false;

	/*
	 * Lookup in the current node
	 * If we have a symbol, then compare the start to match
	 * anywhere inside a function.
	 */
	list_for_each_entry(cnode, &root->val, list) {
		struct symbol *sym;

		if (i == chain->nr)
			break;

		sym = chain->ips[i].ms.sym;

		if (cnode->ms.sym && sym) {
			if (cnode->ms.sym->start != sym->start)
				break;
		} else if (cnode->ip != chain->ips[i].ip)
			break;

		if (!found)
			found = true;
		i++;
	}

	/* matches not, relay on the parent */
	if (!found)
		return -1;

	/* we match only a part of the node. Split it and add the new chain */
	if (i - start < root->val_nr) {
		split_add_child(root, chain, cnode, start, i - start, period);
		return 0;
	}

	/* we match 100% of the path, increment the hit */
	if (i - start == root->val_nr && i == chain->nr) {
		root->hit += period;
		return 0;
	}

	/* We match the node and still have a part remaining */
	__append_chain_children(root, chain, i, period);

	return 0;
}

static void filter_context(struct ip_callchain *old, struct resolved_chain *new,
			   struct map_symbol *syms)
{
	int i, j = 0;

	for (i = 0; i < (int)old->nr; i++) {
		if (old->ips[i] >= PERF_CONTEXT_MAX)
			continue;

		new->ips[j].ip = old->ips[i];
		new->ips[j].ms = syms[i];
		j++;
	}

	new->nr = j;
}


int append_chain(struct callchain_node *root, struct ip_callchain *chain,
		 struct map_symbol *syms, u64 period)
{
	struct resolved_chain *filtered;

	if (!chain->nr)
		return 0;

	filtered = zalloc(sizeof(*filtered) +
			  chain->nr * sizeof(struct resolved_ip));
	if (!filtered)
		return -ENOMEM;

	filter_context(chain, filtered, syms);

	if (!filtered->nr)
		goto end;

	__append_chain_children(root, filtered, 0, period);
end:
	free(filtered);

	return 0;
}
