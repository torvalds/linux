/*
 * Based on intlist.c by:
 * (c) 2009 Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * Licensed under the GPLv2.
 */

#include <errno.h>
#include <stdlib.h>
#include <linux/compiler.h>

#include "intlist.h"

static struct rb_node *intlist__node_new(struct rblist *rblist __maybe_unused,
					 const void *entry)
{
	int i = (int)((long)entry);
	struct rb_node *rc = NULL;
	struct int_node *node = malloc(sizeof(*node));

	if (node != NULL) {
		node->i = i;
		rc = &node->rb_node;
	}

	return rc;
}

static void int_node__delete(struct int_node *ilist)
{
	free(ilist);
}

static void intlist__node_delete(struct rblist *rblist __maybe_unused,
				 struct rb_node *rb_node)
{
	struct int_node *node = container_of(rb_node, struct int_node, rb_node);

	int_node__delete(node);
}

static int intlist__node_cmp(struct rb_node *rb_node, const void *entry)
{
	int i = (int)((long)entry);
	struct int_node *node = container_of(rb_node, struct int_node, rb_node);

	return node->i - i;
}

int intlist__add(struct intlist *ilist, int i)
{
	return rblist__add_node(&ilist->rblist, (void *)((long)i));
}

void intlist__remove(struct intlist *ilist, struct int_node *node)
{
	rblist__remove_node(&ilist->rblist, &node->rb_node);
}

struct int_node *intlist__find(struct intlist *ilist, int i)
{
	struct int_node *node;
	struct rb_node *rb_node;

	if (ilist == NULL)
		return NULL;

	node = NULL;
	rb_node = rblist__find(&ilist->rblist, (void *)((long)i));
	if (rb_node)
		node = container_of(rb_node, struct int_node, rb_node);

	return node;
}

static int intlist__parse_list(struct intlist *ilist, const char *s)
{
	char *sep;
	int err;

	do {
		long value = strtol(s, &sep, 10);
		err = -EINVAL;
		if (*sep != ',' && *sep != '\0')
			break;
		err = intlist__add(ilist, value);
		if (err)
			break;
		s = sep + 1;
	} while (*sep != '\0');

	return err;
}

struct intlist *intlist__new(const char *slist)
{
	struct intlist *ilist = malloc(sizeof(*ilist));

	if (ilist != NULL) {
		rblist__init(&ilist->rblist);
		ilist->rblist.node_cmp    = intlist__node_cmp;
		ilist->rblist.node_new    = intlist__node_new;
		ilist->rblist.node_delete = intlist__node_delete;

		if (slist && intlist__parse_list(ilist, slist))
			goto out_delete;
	}

	return ilist;
out_delete:
	intlist__delete(ilist);
	return NULL;
}

void intlist__delete(struct intlist *ilist)
{
	if (ilist != NULL)
		rblist__delete(&ilist->rblist);
}

struct int_node *intlist__entry(const struct intlist *ilist, unsigned int idx)
{
	struct int_node *node = NULL;
	struct rb_node *rb_node;

	rb_node = rblist__entry(&ilist->rblist, idx);
	if (rb_node)
		node = container_of(rb_node, struct int_node, rb_node);

	return node;
}
