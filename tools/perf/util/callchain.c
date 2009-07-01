/*
 * Copyright (C) 2009, Frederic Weisbecker <fweisbec@gmail.com>
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

#include "callchain.h"


static void
rb_insert_callchain(struct rb_root *root, struct callchain_node *chain)
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

/*
 * Create a child for a parent. If inherit_children, then the new child
 * will become the new parent of it's parent children
 */
static struct callchain_node *
create_child(struct callchain_node *parent, bool inherit_children)
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

	if (inherit_children) {
		struct callchain_node *next;

		list_splice(&parent->children, &new->children);
		INIT_LIST_HEAD(&parent->children);

		list_for_each_entry(next, &new->children, brothers)
			next->parent = new;
	}
	list_add_tail(&new->brothers, &parent->children);

	return new;
}

/*
 * Fill the node with callchain values
 */
static void
fill_node(struct callchain_node *node, struct ip_callchain *chain,
	  int start, struct symbol **syms)
{
	int i;

	for (i = start; i < chain->nr; i++) {
		struct callchain_list *call;

		call = malloc(sizeof(*call));
		if (!call) {
			perror("not enough memory for the code path tree");
			return;
		}
		call->ip = chain->ips[i];
		call->sym = syms[i];
		list_add_tail(&call->list, &node->val);
	}
	node->val_nr = chain->nr - start;
	if (!node->val_nr)
		printf("Warning: empty node in callchain tree\n");
}

static void
add_child(struct callchain_node *parent, struct ip_callchain *chain,
	  int start, struct symbol **syms)
{
	struct callchain_node *new;

	new = create_child(parent, false);
	fill_node(new, chain, start, syms);

	new->hit = 1;
}

/*
 * Split the parent in two parts (a new child is created) and
 * give a part of its callchain to the created child.
 * Then create another child to host the given callchain of new branch
 */
static void
split_add_child(struct callchain_node *parent, struct ip_callchain *chain,
		struct callchain_list *to_split, int idx_parents, int idx_local,
		struct symbol **syms)
{
	struct callchain_node *new;
	struct list_head *old_tail;
	int idx_total = idx_parents + idx_local;

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
	new->val_nr = parent->val_nr - idx_local;
	parent->val_nr = idx_local;

	/* create a new child for the new branch if any */
	if (idx_total < chain->nr) {
		parent->hit = 0;
		add_child(parent, chain, idx_total, syms);
	} else {
		parent->hit = 1;
	}
}

static int
__append_chain(struct callchain_node *root, struct ip_callchain *chain,
	       int start, struct symbol **syms);

static void
__append_chain_children(struct callchain_node *root, struct ip_callchain *chain,
			struct symbol **syms, int start)
{
	struct callchain_node *rnode;

	/* lookup in childrens */
	list_for_each_entry(rnode, &root->children, brothers) {
		int ret = __append_chain(rnode, chain, start, syms);
		if (!ret)
			return;
	}
	/* nothing in children, add to the current node */
	add_child(root, chain, start, syms);
}

static int
__append_chain(struct callchain_node *root, struct ip_callchain *chain,
	       int start, struct symbol **syms)
{
	struct callchain_list *cnode;
	int i = start;
	bool found = false;

	/*
	 * Lookup in the current node
	 * If we have a symbol, then compare the start to match
	 * anywhere inside a function.
	 */
	list_for_each_entry(cnode, &root->val, list) {
		if (i == chain->nr)
			break;
		if (cnode->sym && syms[i]) {
			if (cnode->sym->start != syms[i]->start)
				break;
		} else if (cnode->ip != chain->ips[i])
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
		split_add_child(root, chain, cnode, start, i - start, syms);
		return 0;
	}

	/* we match 100% of the path, increment the hit */
	if (i - start == root->val_nr && i == chain->nr) {
		root->hit++;
		return 0;
	}

	/* We match the node and still have a part remaining */
	__append_chain_children(root, chain, syms, i);

	return 0;
}

void append_chain(struct callchain_node *root, struct ip_callchain *chain,
		  struct symbol **syms)
{
	__append_chain_children(root, chain, syms, 0);
}
