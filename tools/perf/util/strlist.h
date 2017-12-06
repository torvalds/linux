/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_STRLIST_H
#define __PERF_STRLIST_H

#include <linux/rbtree.h>
#include <stdbool.h>

#include "rblist.h"

struct str_node {
	struct rb_node rb_node;
	const char     *s;
};

struct strlist {
	struct rblist rblist;
	bool	      dupstr;
	bool	      file_only;
};

/*
 * @file_only: When dirname is present, only consider entries as filenames,
 *             that should not be added to the list if dirname/entry is not
 *             found
 */
struct strlist_config {
	bool dont_dupstr;
	bool file_only;
	const char *dirname;
};

struct strlist *strlist__new(const char *slist, const struct strlist_config *config);
void strlist__delete(struct strlist *slist);

void strlist__remove(struct strlist *slist, struct str_node *sn);
int strlist__load(struct strlist *slist, const char *filename);
int strlist__add(struct strlist *slist, const char *str);

struct str_node *strlist__entry(const struct strlist *slist, unsigned int idx);
struct str_node *strlist__find(struct strlist *slist, const char *entry);

static inline bool strlist__has_entry(struct strlist *slist, const char *entry)
{
	return strlist__find(slist, entry) != NULL;
}

static inline bool strlist__empty(const struct strlist *slist)
{
	return rblist__empty(&slist->rblist);
}

static inline unsigned int strlist__nr_entries(const struct strlist *slist)
{
	return rblist__nr_entries(&slist->rblist);
}

/* For strlist iteration */
static inline struct str_node *strlist__first(struct strlist *slist)
{
	struct rb_node *rn = rb_first(&slist->rblist.entries);
	return rn ? rb_entry(rn, struct str_node, rb_node) : NULL;
}
static inline struct str_node *strlist__next(struct str_node *sn)
{
	struct rb_node *rn;
	if (!sn)
		return NULL;
	rn = rb_next(&sn->rb_node);
	return rn ? rb_entry(rn, struct str_node, rb_node) : NULL;
}

/**
 * strlist_for_each      - iterate over a strlist
 * @pos:	the &struct str_node to use as a loop cursor.
 * @slist:	the &struct strlist for loop.
 */
#define strlist__for_each_entry(pos, slist)	\
	for (pos = strlist__first(slist); pos; pos = strlist__next(pos))

/**
 * strlist_for_each_safe - iterate over a strlist safe against removal of
 *                         str_node
 * @pos:	the &struct str_node to use as a loop cursor.
 * @n:		another &struct str_node to use as temporary storage.
 * @slist:	the &struct strlist for loop.
 */
#define strlist__for_each_entry_safe(pos, n, slist)	\
	for (pos = strlist__first(slist), n = strlist__next(pos); pos;\
	     pos = n, n = strlist__next(n))
#endif /* __PERF_STRLIST_H */
