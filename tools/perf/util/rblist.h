/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_RBLIST_H
#define __PERF_RBLIST_H

#include <linux/rbtree.h>
#include <stdbool.h>

/*
 * create yesde structs of the form:
 * struct my_yesde {
 *     struct rb_yesde rb_yesde;
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

	int (*yesde_cmp)(struct rb_yesde *rbn, const void *entry);
	struct rb_yesde *(*yesde_new)(struct rblist *rlist, const void *new_entry);
	void (*yesde_delete)(struct rblist *rblist, struct rb_yesde *rb_yesde);
};

void rblist__init(struct rblist *rblist);
void rblist__exit(struct rblist *rblist);
void rblist__delete(struct rblist *rblist);
int rblist__add_yesde(struct rblist *rblist, const void *new_entry);
void rblist__remove_yesde(struct rblist *rblist, struct rb_yesde *rb_yesde);
struct rb_yesde *rblist__find(struct rblist *rblist, const void *entry);
struct rb_yesde *rblist__findnew(struct rblist *rblist, const void *entry);
struct rb_yesde *rblist__entry(const struct rblist *rblist, unsigned int idx);

static inline bool rblist__empty(const struct rblist *rblist)
{
	return rblist->nr_entries == 0;
}

static inline unsigned int rblist__nr_entries(const struct rblist *rblist)
{
	return rblist->nr_entries;
}

#endif /* __PERF_RBLIST_H */
