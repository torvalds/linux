// SPDX-License-Identifier: GPL-2.0-only
/*
 * Based on intlist.c by:
 * (c) 2009 Arnaldo Carvalho de Melo <acme@redhat.com>
 */

#include <erryes.h>
#include <stdlib.h>
#include <linux/compiler.h>

#include "intlist.h"

static struct rb_yesde *intlist__yesde_new(struct rblist *rblist __maybe_unused,
					 const void *entry)
{
	int i = (int)((long)entry);
	struct rb_yesde *rc = NULL;
	struct int_yesde *yesde = malloc(sizeof(*yesde));

	if (yesde != NULL) {
		yesde->i = i;
		yesde->priv = NULL;
		rc = &yesde->rb_yesde;
	}

	return rc;
}

static void int_yesde__delete(struct int_yesde *ilist)
{
	free(ilist);
}

static void intlist__yesde_delete(struct rblist *rblist __maybe_unused,
				 struct rb_yesde *rb_yesde)
{
	struct int_yesde *yesde = container_of(rb_yesde, struct int_yesde, rb_yesde);

	int_yesde__delete(yesde);
}

static int intlist__yesde_cmp(struct rb_yesde *rb_yesde, const void *entry)
{
	int i = (int)((long)entry);
	struct int_yesde *yesde = container_of(rb_yesde, struct int_yesde, rb_yesde);

	return yesde->i - i;
}

int intlist__add(struct intlist *ilist, int i)
{
	return rblist__add_yesde(&ilist->rblist, (void *)((long)i));
}

void intlist__remove(struct intlist *ilist, struct int_yesde *yesde)
{
	rblist__remove_yesde(&ilist->rblist, &yesde->rb_yesde);
}

static struct int_yesde *__intlist__findnew(struct intlist *ilist,
					   int i, bool create)
{
	struct int_yesde *yesde = NULL;
	struct rb_yesde *rb_yesde;

	if (ilist == NULL)
		return NULL;

	if (create)
		rb_yesde = rblist__findnew(&ilist->rblist, (void *)((long)i));
	else
		rb_yesde = rblist__find(&ilist->rblist, (void *)((long)i));

	if (rb_yesde)
		yesde = container_of(rb_yesde, struct int_yesde, rb_yesde);

	return yesde;
}

struct int_yesde *intlist__find(struct intlist *ilist, int i)
{
	return __intlist__findnew(ilist, i, false);
}

struct int_yesde *intlist__findnew(struct intlist *ilist, int i)
{
	return __intlist__findnew(ilist, i, true);
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
		ilist->rblist.yesde_cmp    = intlist__yesde_cmp;
		ilist->rblist.yesde_new    = intlist__yesde_new;
		ilist->rblist.yesde_delete = intlist__yesde_delete;

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

struct int_yesde *intlist__entry(const struct intlist *ilist, unsigned int idx)
{
	struct int_yesde *yesde = NULL;
	struct rb_yesde *rb_yesde;

	rb_yesde = rblist__entry(&ilist->rblist, idx);
	if (rb_yesde)
		yesde = container_of(rb_yesde, struct int_yesde, rb_yesde);

	return yesde;
}
