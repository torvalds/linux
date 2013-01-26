/*
 * Based on strlist.c by:
 * (c) 2009 Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * Licensed under the GPLv2.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "rblist.h"

int rblist__add_node(struct rblist *rblist, const void *new_entry)
{
	struct rb_node **p = &rblist->entries.rb_node;
	struct rb_node *parent = NULL, *new_node;

	while (*p != NULL) {
		int rc;

		parent = *p;

		rc = rblist->node_cmp(parent, new_entry);
		if (rc > 0)
			p = &(*p)->rb_left;
		else if (rc < 0)
			p = &(*p)->rb_right;
		else
			return -EEXIST;
	}

	new_node = rblist->node_new(rblist, new_entry);
	if (new_node == NULL)
		return -ENOMEM;

	rb_link_node(new_node, parent, p);
	rb_insert_color(new_node, &rblist->entries);
	++rblist->nr_entries;

	return 0;
}

void rblist__remove_node(struct rblist *rblist, struct rb_node *rb_node)
{
	rb_erase(rb_node, &rblist->entries);
	--rblist->nr_entries;
	rblist->node_delete(rblist, rb_node);
}

struct rb_node *rblist__find(struct rblist *rblist, const void *entry)
{
	struct rb_node **p = &rblist->entries.rb_node;
	struct rb_node *parent = NULL;

	while (*p != NULL) {
		int rc;

		parent = *p;

		rc = rblist->node_cmp(parent, entry);
		if (rc > 0)
			p = &(*p)->rb_left;
		else if (rc < 0)
			p = &(*p)->rb_right;
		else
			return parent;
	}

	return NULL;
}

void rblist__init(struct rblist *rblist)
{
	if (rblist != NULL) {
		rblist->entries	 = RB_ROOT;
		rblist->nr_entries = 0;
	}

	return;
}

void rblist__delete(struct rblist *rblist)
{
	if (rblist != NULL) {
		struct rb_node *pos, *next = rb_first(&rblist->entries);

		while (next) {
			pos = next;
			next = rb_next(pos);
			rblist__remove_node(rblist, pos);
		}
		free(rblist);
	}
}

struct rb_node *rblist__entry(const struct rblist *rblist, unsigned int idx)
{
	struct rb_node *node;

	for (node = rb_first(&rblist->entries); node; node = rb_next(node)) {
		if (!idx--)
			return node;
	}

	return NULL;
}
