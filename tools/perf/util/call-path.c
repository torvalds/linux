// SPDX-License-Identifier: GPL-2.0-only
/*
 * call-path.h: Manipulate a tree data structure containing function call paths
 * Copyright (c) 2014, Intel Corporation.
 */

#include <linux/rbtree.h>
#include <linux/list.h>
#include <linux/zalloc.h>
#include <stdlib.h>

#include "call-path.h"

static void call_path__init(struct call_path *cp, struct call_path *parent,
			    struct symbol *sym, u64 ip, bool in_kernel)
{
	cp->parent = parent;
	cp->sym = sym;
	cp->ip = sym ? 0 : ip;
	cp->db_id = 0;
	cp->in_kernel = in_kernel;
	RB_CLEAR_NODE(&cp->rb_node);
	cp->children = RB_ROOT;
}

struct call_path_root *call_path_root__new(void)
{
	struct call_path_root *cpr;

	cpr = zalloc(sizeof(struct call_path_root));
	if (!cpr)
		return NULL;
	call_path__init(&cpr->call_path, NULL, NULL, 0, false);
	INIT_LIST_HEAD(&cpr->blocks);
	return cpr;
}

void call_path_root__free(struct call_path_root *cpr)
{
	struct call_path_block *pos, *n;

	list_for_each_entry_safe(pos, n, &cpr->blocks, node) {
		list_del(&pos->node);
		free(pos);
	}
	free(cpr);
}

static struct call_path *call_path__new(struct call_path_root *cpr,
					struct call_path *parent,
					struct symbol *sym, u64 ip,
					bool in_kernel)
{
	struct call_path_block *cpb;
	struct call_path *cp;
	size_t n;

	if (cpr->next < cpr->sz) {
		cpb = list_last_entry(&cpr->blocks, struct call_path_block,
				      node);
	} else {
		cpb = zalloc(sizeof(struct call_path_block));
		if (!cpb)
			return NULL;
		list_add_tail(&cpb->node, &cpr->blocks);
		cpr->sz += CALL_PATH_BLOCK_SIZE;
	}

	n = cpr->next++ & CALL_PATH_BLOCK_MASK;
	cp = &cpb->cp[n];

	call_path__init(cp, parent, sym, ip, in_kernel);

	return cp;
}

struct call_path *call_path__findnew(struct call_path_root *cpr,
				     struct call_path *parent,
				     struct symbol *sym, u64 ip, u64 ks)
{
	struct rb_node **p;
	struct rb_node *node_parent = NULL;
	struct call_path *cp;
	bool in_kernel = ip >= ks;

	if (sym)
		ip = 0;

	if (!parent)
		return call_path__new(cpr, parent, sym, ip, in_kernel);

	p = &parent->children.rb_node;
	while (*p != NULL) {
		node_parent = *p;
		cp = rb_entry(node_parent, struct call_path, rb_node);

		if (cp->sym == sym && cp->ip == ip)
			return cp;

		if (sym < cp->sym || (sym == cp->sym && ip < cp->ip))
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	cp = call_path__new(cpr, parent, sym, ip, in_kernel);
	if (!cp)
		return NULL;

	rb_link_node(&cp->rb_node, node_parent, p);
	rb_insert_color(&cp->rb_node, &parent->children);

	return cp;
}
