/*
 * Copyright (C) 2009, Frederic Weisbecker <fweisbec@gmail.com>
 *
 * Handle the callchains from the stream in an ad-hoc radix tree and then
 * sort them in an rbtree.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>

#include "callchain.h"


static void rb_insert_callchain(struct rb_root *root, struct callchain_node *chain)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct callchain_node *rnode;

	while (*p) {
		parent = *p;
		rnode = rb_entry(parent, struct callchain_node, rb_node);

		if (rnode->hit < chain->hit)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	rb_link_node(&chain->rb_node, parent, p);
	rb_insert_color(&chain->rb_node, root);
}

/*
 * Once we get every callchains from the stream, we can now
 * sort them by hit
 */
void sort_chain_to_rbtree(struct rb_root *rb_root, struct callchain_node *node)
{
	struct callchain_node *child;

	list_for_each_entry(child, &node->children, brothers)
		sort_chain_to_rbtree(rb_root, child);

	if (node->hit)
		rb_insert_callchain(rb_root, node);
}

static struct callchain_node *create_child(struct callchain_node *parent)
{
	struct callchain_node *new;

	new = malloc(sizeof(*new));
	if (!new) {
		perror("not enough memory to create child for code path tree");
		return NULL;
	}
	new->parent = parent;
	INIT_LIST_HEAD(&new->children);
	INIT_LIST_HEAD(&new->val);
	list_add_tail(&new->brothers, &parent->children);

	return new;
}

static void
fill_node(struct callchain_node *node, struct ip_callchain *chain, int start)
{
	int i;

	for (i = start; i < chain->nr; i++) {
		struct callchain_list *call;

		call = malloc(sizeof(*chain));
		if (!call) {
			perror("not enough memory for the code path tree");
			return;
		}
		call->ip = chain->ips[i];
		list_add_tail(&call->list, &node->val);
	}
	node->val_nr = i - start;
}

static void add_child(struct callchain_node *parent, struct ip_callchain *chain)
{
	struct callchain_node *new;

	new = create_child(parent);
	fill_node(new, chain, parent->val_nr);

	new->hit = 1;
}

static void
split_add_child(struct callchain_node *parent, struct ip_callchain *chain,
		struct callchain_list *to_split, int idx)
{
	struct callchain_node *new;

	/* split */
	new = create_child(parent);
	list_move_tail(&to_split->list, &new->val);
	new->hit = parent->hit;
	parent->hit = 0;
	parent->val_nr = idx;

	/* create the new one */
	add_child(parent, chain);
}

static int
__append_chain(struct callchain_node *root, struct ip_callchain *chain,
		int start);

static int
__append_chain_children(struct callchain_node *root, struct ip_callchain *chain)
{
	struct callchain_node *rnode;

	/* lookup in childrens */
	list_for_each_entry(rnode, &root->children, brothers) {
		int ret = __append_chain(rnode, chain, root->val_nr);
		if (!ret)
			return 0;
	}
	return -1;
}

static int
__append_chain(struct callchain_node *root, struct ip_callchain *chain,
		int start)
{
	struct callchain_list *cnode;
	int i = start;
	bool found = false;

	/* lookup in the current node */
	list_for_each_entry(cnode, &root->val, list) {
		if (cnode->ip != chain->ips[i++])
			break;
		if (!found)
			found = true;
		if (i == chain->nr)
			break;
	}

	/* matches not, relay on the parent */
	if (!found)
		return -1;

	/* we match only a part of the node. Split it and add the new chain */
	if (i < root->val_nr) {
		split_add_child(root, chain, cnode, i);
		return 0;
	}

	/* we match 100% of the path, increment the hit */
	if (i == root->val_nr) {
		root->hit++;
		return 0;
	}

	return __append_chain_children(root, chain);
}

void append_chain(struct callchain_node *root, struct ip_callchain *chain)
{
	if (__append_chain_children(root, chain) == -1)
		add_child(root, chain);
}
