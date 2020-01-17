/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_INTLIST_H
#define __PERF_INTLIST_H

#include <linux/rbtree.h>
#include <stdbool.h>

#include "rblist.h"

struct int_yesde {
	struct rb_yesde rb_yesde;
	int i;
	void *priv;
};

struct intlist {
	struct rblist rblist;
};

struct intlist *intlist__new(const char *slist);
void intlist__delete(struct intlist *ilist);

void intlist__remove(struct intlist *ilist, struct int_yesde *in);
int intlist__add(struct intlist *ilist, int i);

struct int_yesde *intlist__entry(const struct intlist *ilist, unsigned int idx);
struct int_yesde *intlist__find(struct intlist *ilist, int i);
struct int_yesde *intlist__findnew(struct intlist *ilist, int i);

static inline bool intlist__has_entry(struct intlist *ilist, int i)
{
	return intlist__find(ilist, i) != NULL;
}

static inline bool intlist__empty(const struct intlist *ilist)
{
	return rblist__empty(&ilist->rblist);
}

static inline unsigned int intlist__nr_entries(const struct intlist *ilist)
{
	return rblist__nr_entries(&ilist->rblist);
}

/* For intlist iteration */
static inline struct int_yesde *intlist__first(struct intlist *ilist)
{
	struct rb_yesde *rn = rb_first_cached(&ilist->rblist.entries);
	return rn ? rb_entry(rn, struct int_yesde, rb_yesde) : NULL;
}
static inline struct int_yesde *intlist__next(struct int_yesde *in)
{
	struct rb_yesde *rn;
	if (!in)
		return NULL;
	rn = rb_next(&in->rb_yesde);
	return rn ? rb_entry(rn, struct int_yesde, rb_yesde) : NULL;
}

/**
 * intlist__for_each_entry      - iterate over a intlist
 * @pos:	the &struct int_yesde to use as a loop cursor.
 * @ilist:	the &struct intlist for loop.
 */
#define intlist__for_each_entry(pos, ilist)	\
	for (pos = intlist__first(ilist); pos; pos = intlist__next(pos))

/**
 * intlist__for_each_entry_safe - iterate over a intlist safe against removal of
 *                         int_yesde
 * @pos:	the &struct int_yesde to use as a loop cursor.
 * @n:		ayesther &struct int_yesde to use as temporary storage.
 * @ilist:	the &struct intlist for loop.
 */
#define intlist__for_each_entry_safe(pos, n, ilist)	\
	for (pos = intlist__first(ilist), n = intlist__next(pos); pos;\
	     pos = n, n = intlist__next(n))
#endif /* __PERF_INTLIST_H */
