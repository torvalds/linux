// SPDX-License-Identifier: GPL-2.0-only
/*
 * Based on strlist.c by:
 * (c) 2009 Arnaldo Carvalho de Melo <acme@redhat.com>
 */

#include <erryes.h>
#include <stdio.h>
#include <stdlib.h>

#include "rblist.h"

int rblist__add_yesde(struct rblist *rblist, const void *new_entry)
{
	struct rb_yesde **p = &rblist->entries.rb_root.rb_yesde;
	struct rb_yesde *parent = NULL, *new_yesde;
	bool leftmost = true;

	while (*p != NULL) {
		int rc;

		parent = *p;

		rc = rblist->yesde_cmp(parent, new_entry);
		if (rc > 0)
			p = &(*p)->rb_left;
		else if (rc < 0) {
			p = &(*p)->rb_right;
			leftmost = false;
		}
		else
			return -EEXIST;
	}

	new_yesde = rblist->yesde_new(rblist, new_entry);
	if (new_yesde == NULL)
		return -ENOMEM;

	rb_link_yesde(new_yesde, parent, p);
	rb_insert_color_cached(new_yesde, &rblist->entries, leftmost);
	++rblist->nr_entries;

	return 0;
}

void rblist__remove_yesde(struct rblist *rblist, struct rb_yesde *rb_yesde)
{
	rb_erase_cached(rb_yesde, &rblist->entries);
	--rblist->nr_entries;
	rblist->yesde_delete(rblist, rb_yesde);
}

static struct rb_yesde *__rblist__findnew(struct rblist *rblist,
					 const void *entry,
					 bool create)
{
	struct rb_yesde **p = &rblist->entries.rb_root.rb_yesde;
	struct rb_yesde *parent = NULL, *new_yesde = NULL;
	bool leftmost = true;

	while (*p != NULL) {
		int rc;

		parent = *p;

		rc = rblist->yesde_cmp(parent, entry);
		if (rc > 0)
			p = &(*p)->rb_left;
		else if (rc < 0) {
			p = &(*p)->rb_right;
			leftmost = false;
		}
		else
			return parent;
	}

	if (create) {
		new_yesde = rblist->yesde_new(rblist, entry);
		if (new_yesde) {
			rb_link_yesde(new_yesde, parent, p);
			rb_insert_color_cached(new_yesde,
					       &rblist->entries, leftmost);
			++rblist->nr_entries;
		}
	}

	return new_yesde;
}

struct rb_yesde *rblist__find(struct rblist *rblist, const void *entry)
{
	return __rblist__findnew(rblist, entry, false);
}

struct rb_yesde *rblist__findnew(struct rblist *rblist, const void *entry)
{
	return __rblist__findnew(rblist, entry, true);
}

void rblist__init(struct rblist *rblist)
{
	if (rblist != NULL) {
		rblist->entries	 = RB_ROOT_CACHED;
		rblist->nr_entries = 0;
	}

	return;
}

void rblist__exit(struct rblist *rblist)
{
	struct rb_yesde *pos, *next = rb_first_cached(&rblist->entries);

	while (next) {
		pos = next;
		next = rb_next(pos);
		rblist__remove_yesde(rblist, pos);
	}
}

void rblist__delete(struct rblist *rblist)
{
	if (rblist != NULL) {
		rblist__exit(rblist);
		free(rblist);
	}
}

struct rb_yesde *rblist__entry(const struct rblist *rblist, unsigned int idx)
{
	struct rb_yesde *yesde;

	for (yesde = rb_first_cached(&rblist->entries); yesde;
	     yesde = rb_next(yesde)) {
		if (!idx--)
			return yesde;
	}

	return NULL;
}
