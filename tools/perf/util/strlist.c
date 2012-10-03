/*
 * (c) 2009 Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * Licensed under the GPLv2.
 */

#include "strlist.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static
struct rb_node *strlist__node_new(struct rblist *rblist, const void *entry)
{
	const char *s = entry;
	struct rb_node *rc = NULL;
	struct strlist *strlist = container_of(rblist, struct strlist, rblist);
	struct str_node *snode = malloc(sizeof(*snode));

	if (snode != NULL) {
		if (strlist->dupstr) {
			s = strdup(s);
			if (s == NULL)
				goto out_delete;
		}
		snode->s = s;
		rc = &snode->rb_node;
	}

	return rc;

out_delete:
	free(snode);
	return NULL;
}

static void str_node__delete(struct str_node *self, bool dupstr)
{
	if (dupstr)
		free((void *)self->s);
	free(self);
}

static
void strlist__node_delete(struct rblist *rblist, struct rb_node *rb_node)
{
	struct strlist *slist = container_of(rblist, struct strlist, rblist);
	struct str_node *snode = container_of(rb_node, struct str_node, rb_node);

	str_node__delete(snode, slist->dupstr);
}

static int strlist__node_cmp(struct rb_node *rb_node, const void *entry)
{
	const char *str = entry;
	struct str_node *snode = container_of(rb_node, struct str_node, rb_node);

	return strcmp(snode->s, str);
}

int strlist__add(struct strlist *self, const char *new_entry)
{
	return rblist__add_node(&self->rblist, new_entry);
}

int strlist__load(struct strlist *self, const char *filename)
{
	char entry[1024];
	int err;
	FILE *fp = fopen(filename, "r");

	if (fp == NULL)
		return errno;

	while (fgets(entry, sizeof(entry), fp) != NULL) {
		const size_t len = strlen(entry);

		if (len == 0)
			continue;
		entry[len - 1] = '\0';

		err = strlist__add(self, entry);
		if (err != 0)
			goto out;
	}

	err = 0;
out:
	fclose(fp);
	return err;
}

void strlist__remove(struct strlist *slist, struct str_node *snode)
{
	rblist__remove_node(&slist->rblist, &snode->rb_node);
}

struct str_node *strlist__find(struct strlist *slist, const char *entry)
{
	struct str_node *snode = NULL;
	struct rb_node *rb_node = rblist__find(&slist->rblist, entry);

	if (rb_node)
		snode = container_of(rb_node, struct str_node, rb_node);

	return snode;
}

static int strlist__parse_list_entry(struct strlist *self, const char *s)
{
	if (strncmp(s, "file://", 7) == 0)
		return strlist__load(self, s + 7);

	return strlist__add(self, s);
}

int strlist__parse_list(struct strlist *self, const char *s)
{
	char *sep;
	int err;

	while ((sep = strchr(s, ',')) != NULL) {
		*sep = '\0';
		err = strlist__parse_list_entry(self, s);
		*sep = ',';
		if (err != 0)
			return err;
		s = sep + 1;
	}

	return *s ? strlist__parse_list_entry(self, s) : 0;
}

struct strlist *strlist__new(bool dupstr, const char *slist)
{
	struct strlist *self = malloc(sizeof(*self));

	if (self != NULL) {
		rblist__init(&self->rblist);
		self->rblist.node_cmp    = strlist__node_cmp;
		self->rblist.node_new    = strlist__node_new;
		self->rblist.node_delete = strlist__node_delete;

		self->dupstr	 = dupstr;
		if (slist && strlist__parse_list(self, slist) != 0)
			goto out_error;
	}

	return self;
out_error:
	free(self);
	return NULL;
}

void strlist__delete(struct strlist *self)
{
	if (self != NULL)
		rblist__delete(&self->rblist);
}

struct str_node *strlist__entry(const struct strlist *slist, unsigned int idx)
{
	struct str_node *snode = NULL;
	struct rb_node *rb_node;

	rb_node = rblist__entry(&slist->rblist, idx);
	if (rb_node)
		snode = container_of(rb_node, struct str_node, rb_node);

	return snode;
}
