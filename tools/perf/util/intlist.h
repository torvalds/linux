#ifndef __PERF_INTLIST_H
#define __PERF_INTLIST_H

#include <linux/rbtree.h>
#include <stdbool.h>

#include "rblist.h"

struct int_node {
	struct rb_node rb_node;
	int i;
};

struct intlist {
	struct rblist rblist;
};

struct intlist *intlist__new(void);
void intlist__delete(struct intlist *ilist);

void intlist__remove(struct intlist *ilist, struct int_node *in);
int intlist__add(struct intlist *ilist, int i);

struct int_node *intlist__entry(const struct intlist *ilist, unsigned int idx);
struct int_node *intlist__find(struct intlist *ilist, int i);

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
static inline struct int_node *intlist__first(struct intlist *ilist)
{
	struct rb_node *rn = rb_first(&ilist->rblist.entries);
	return rn ? rb_entry(rn, struct int_node, rb_node) : NULL;
}
static inline struct int_node *intlist__next(struct int_node *in)
{
	struct rb_node *rn;
	if (!in)
		return NULL;
	rn = rb_next(&in->rb_node);
	return rn ? rb_entry(rn, struct int_node, rb_node) : NULL;
}

/**
 * intlist_for_each      - iterate over a intlist
 * @pos:	the &struct int_node to use as a loop cursor.
 * @ilist:	the &struct intlist for loop.
 */
#define intlist__for_each(pos, ilist)	\
	for (pos = intlist__first(ilist); pos; pos = intlist__next(pos))

/**
 * intlist_for_each_safe - iterate over a intlist safe against removal of
 *                         int_node
 * @pos:	the &struct int_node to use as a loop cursor.
 * @n:		another &struct int_node to use as temporary storage.
 * @ilist:	the &struct intlist for loop.
 */
#define intlist__for_each_safe(pos, n, ilist)	\
	for (pos = intlist__first(ilist), n = intlist__next(pos); pos;\
	     pos = n, n = intlist__next(n))
#endif /* __PERF_INTLIST_H */
