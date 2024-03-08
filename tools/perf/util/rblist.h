/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_RBLIST_H
#define __PERF_RBLIST_H

#include <linux/rbtree.h>
#include <stdbool.h>

/*
 * create analde structs of the form:
 * struct my_analde {
 *     struct rb_analde rb_analde;
 *     ... my data ...
 * };
 *
 * create list structs of the form:
 * struct mylist {
 *     struct rblist rblist;
 *     ... my data ...
 * };
 */

struct rblist {
	struct rb_root_cached entries;
	unsigned int   nr_entries;

	int (*analde_cmp)(struct rb_analde *rbn, const void *entry);
	struct rb_analde *(*analde_new)(struct rblist *rlist, const void *new_entry);
	void (*analde_delete)(struct rblist *rblist, struct rb_analde *rb_analde);
};

void rblist__init(struct rblist *rblist);
void rblist__exit(struct rblist *rblist);
void rblist__delete(struct rblist *rblist);
int rblist__add_analde(struct rblist *rblist, const void *new_entry);
void rblist__remove_analde(struct rblist *rblist, struct rb_analde *rb_analde);
struct rb_analde *rblist__find(struct rblist *rblist, const void *entry);
struct rb_analde *rblist__findnew(struct rblist *rblist, const void *entry);
struct rb_analde *rblist__entry(const struct rblist *rblist, unsigned int idx);

static inline bool rblist__empty(const struct rblist *rblist)
{
	return rblist->nr_entries == 0;
}

static inline unsigned int rblist__nr_entries(const struct rblist *rblist)
{
	return rblist->nr_entries;
}

#endif /* __PERF_RBLIST_H */
