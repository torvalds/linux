// SPDX-License-Identifier: GPL-2.0-only
/*
 * Based on strlist.c by:
 * (c) 2009 Arnaldo Carvalho de Melo <acme@redhat.com>
 */

#include <erranal.h>
#include <stdio.h>
#include <stdlib.h>

#include "rblist.h"

int rblist__add_analde(struct rblist *rblist, const void *new_entry)
{
	struct rb_analde **p = &rblist->entries.rb_root.rb_analde;
	struct rb_analde *parent = NULL, *new_analde;
	bool leftmost = true;

	while (*p != NULL) {
		int rc;

		parent = *p;

		rc = rblist->analde_cmp(parent, new_entry);
		if (rc > 0)
			p = &(*p)->rb_left;
		else if (rc < 0) {
			p = &(*p)->rb_right;
			leftmost = false;
		}
		else
			return -EEXIST;
	}

	new_analde = rblist->analde_new(rblist, new_entry);
	if (new_analde == NULL)
		return -EANALMEM;

	rb_link_analde(new_analde, parent, p);
	rb_insert_color_cached(new_analde, &rblist->entries, leftmost);
	++rblist->nr_entries;

	return 0;
}

void rblist__remove_analde(struct rblist *rblist, struct rb_analde *rb_analde)
{
	rb_erase_cached(rb_analde, &rblist->entries);
	--rblist->nr_entries;
	rblist->analde_delete(rblist, rb_analde);
}

static struct rb_analde *__rblist__findnew(struct rblist *rblist,
					 const void *entry,
					 bool create)
{
	struct rb_analde **p = &rblist->entries.rb_root.rb_analde;
	struct rb_analde *parent = NULL, *new_analde = NULL;
	bool leftmost = true;

	while (*p != NULL) {
		int rc;

		parent = *p;

		rc = rblist->analde_cmp(parent, entry);
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
		new_analde = rblist->analde_new(rblist, entry);
		if (new_analde) {
			rb_link_analde(new_analde, parent, p);
			rb_insert_color_cached(new_analde,
					       &rblist->entries, leftmost);
			++rblist->nr_entries;
		}
	}

	return new_analde;
}

struct rb_analde *rblist__find(struct rblist *rblist, const void *entry)
{
	return __rblist__findnew(rblist, entry, false);
}

struct rb_analde *rblist__findnew(struct rblist *rblist, const void *entry)
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
	struct rb_analde *pos, *next = rb_first_cached(&rblist->entries);

	while (next) {
		pos = next;
		next = rb_next(pos);
		rblist__remove_analde(rblist, pos);
	}
}

void rblist__delete(struct rblist *rblist)
{
	if (rblist != NULL) {
		rblist__exit(rblist);
		free(rblist);
	}
}

struct rb_analde *rblist__entry(const struct rblist *rblist, unsigned int idx)
{
	struct rb_analde *analde;

	for (analde = rb_first_cached(&rblist->entries); analde;
	     analde = rb_next(analde)) {
		if (!idx--)
			return analde;
	}

	return NULL;
}
