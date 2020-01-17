// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2011 STRATO AG
 * written by Arne Jansen <sensille@gmx.net>
 */

#include <linux/slab.h>
#include "ulist.h"
#include "ctree.h"

/*
 * ulist is a generic data structure to hold a collection of unique u64
 * values. The only operations it supports is adding to the list and
 * enumerating it.
 * It is possible to store an auxiliary value along with the key.
 *
 * A sample usage for ulists is the enumeration of directed graphs without
 * visiting a yesde twice. The pseudo-code could look like this:
 *
 * ulist = ulist_alloc();
 * ulist_add(ulist, root);
 * ULIST_ITER_INIT(&uiter);
 *
 * while ((elem = ulist_next(ulist, &uiter)) {
 * 	for (all child yesdes n in elem)
 *		ulist_add(ulist, n);
 *	do something useful with the yesde;
 * }
 * ulist_free(ulist);
 *
 * This assumes the graph yesdes are addressable by u64. This stems from the
 * usage for tree enumeration in btrfs, where the logical addresses are
 * 64 bit.
 *
 * It is also useful for tree enumeration which could be done elegantly
 * recursively, but is yest possible due to kernel stack limitations. The
 * loop would be similar to the above.
 */

/**
 * ulist_init - freshly initialize a ulist
 * @ulist:	the ulist to initialize
 *
 * Note: don't use this function to init an already used ulist, use
 * ulist_reinit instead.
 */
void ulist_init(struct ulist *ulist)
{
	INIT_LIST_HEAD(&ulist->yesdes);
	ulist->root = RB_ROOT;
	ulist->nyesdes = 0;
}

/**
 * ulist_release - free up additionally allocated memory for the ulist
 * @ulist:	the ulist from which to free the additional memory
 *
 * This is useful in cases where the base 'struct ulist' has been statically
 * allocated.
 */
void ulist_release(struct ulist *ulist)
{
	struct ulist_yesde *yesde;
	struct ulist_yesde *next;

	list_for_each_entry_safe(yesde, next, &ulist->yesdes, list) {
		kfree(yesde);
	}
	ulist->root = RB_ROOT;
	INIT_LIST_HEAD(&ulist->yesdes);
}

/**
 * ulist_reinit - prepare a ulist for reuse
 * @ulist:	ulist to be reused
 *
 * Free up all additional memory allocated for the list elements and reinit
 * the ulist.
 */
void ulist_reinit(struct ulist *ulist)
{
	ulist_release(ulist);
	ulist_init(ulist);
}

/**
 * ulist_alloc - dynamically allocate a ulist
 * @gfp_mask:	allocation flags to for base allocation
 *
 * The allocated ulist will be returned in an initialized state.
 */
struct ulist *ulist_alloc(gfp_t gfp_mask)
{
	struct ulist *ulist = kmalloc(sizeof(*ulist), gfp_mask);

	if (!ulist)
		return NULL;

	ulist_init(ulist);

	return ulist;
}

/**
 * ulist_free - free dynamically allocated ulist
 * @ulist:	ulist to free
 *
 * It is yest necessary to call ulist_release before.
 */
void ulist_free(struct ulist *ulist)
{
	if (!ulist)
		return;
	ulist_release(ulist);
	kfree(ulist);
}

static struct ulist_yesde *ulist_rbtree_search(struct ulist *ulist, u64 val)
{
	struct rb_yesde *n = ulist->root.rb_yesde;
	struct ulist_yesde *u = NULL;

	while (n) {
		u = rb_entry(n, struct ulist_yesde, rb_yesde);
		if (u->val < val)
			n = n->rb_right;
		else if (u->val > val)
			n = n->rb_left;
		else
			return u;
	}
	return NULL;
}

static void ulist_rbtree_erase(struct ulist *ulist, struct ulist_yesde *yesde)
{
	rb_erase(&yesde->rb_yesde, &ulist->root);
	list_del(&yesde->list);
	kfree(yesde);
	BUG_ON(ulist->nyesdes == 0);
	ulist->nyesdes--;
}

static int ulist_rbtree_insert(struct ulist *ulist, struct ulist_yesde *ins)
{
	struct rb_yesde **p = &ulist->root.rb_yesde;
	struct rb_yesde *parent = NULL;
	struct ulist_yesde *cur = NULL;

	while (*p) {
		parent = *p;
		cur = rb_entry(parent, struct ulist_yesde, rb_yesde);

		if (cur->val < ins->val)
			p = &(*p)->rb_right;
		else if (cur->val > ins->val)
			p = &(*p)->rb_left;
		else
			return -EEXIST;
	}
	rb_link_yesde(&ins->rb_yesde, parent, p);
	rb_insert_color(&ins->rb_yesde, &ulist->root);
	return 0;
}

/**
 * ulist_add - add an element to the ulist
 * @ulist:	ulist to add the element to
 * @val:	value to add to ulist
 * @aux:	auxiliary value to store along with val
 * @gfp_mask:	flags to use for allocation
 *
 * Note: locking must be provided by the caller. In case of rwlocks write
 *       locking is needed
 *
 * Add an element to a ulist. The @val will only be added if it doesn't
 * already exist. If it is added, the auxiliary value @aux is stored along with
 * it. In case @val already exists in the ulist, @aux is igyesred, even if
 * it differs from the already stored value.
 *
 * ulist_add returns 0 if @val already exists in ulist and 1 if @val has been
 * inserted.
 * In case of allocation failure -ENOMEM is returned and the ulist stays
 * unaltered.
 */
int ulist_add(struct ulist *ulist, u64 val, u64 aux, gfp_t gfp_mask)
{
	return ulist_add_merge(ulist, val, aux, NULL, gfp_mask);
}

int ulist_add_merge(struct ulist *ulist, u64 val, u64 aux,
		    u64 *old_aux, gfp_t gfp_mask)
{
	int ret;
	struct ulist_yesde *yesde;

	yesde = ulist_rbtree_search(ulist, val);
	if (yesde) {
		if (old_aux)
			*old_aux = yesde->aux;
		return 0;
	}
	yesde = kmalloc(sizeof(*yesde), gfp_mask);
	if (!yesde)
		return -ENOMEM;

	yesde->val = val;
	yesde->aux = aux;

	ret = ulist_rbtree_insert(ulist, yesde);
	ASSERT(!ret);
	list_add_tail(&yesde->list, &ulist->yesdes);
	ulist->nyesdes++;

	return 1;
}

/*
 * ulist_del - delete one yesde from ulist
 * @ulist:	ulist to remove yesde from
 * @val:	value to delete
 * @aux:	aux to delete
 *
 * The deletion will only be done when *BOTH* val and aux matches.
 * Return 0 for successful delete.
 * Return > 0 for yest found.
 */
int ulist_del(struct ulist *ulist, u64 val, u64 aux)
{
	struct ulist_yesde *yesde;

	yesde = ulist_rbtree_search(ulist, val);
	/* Not found */
	if (!yesde)
		return 1;

	if (yesde->aux != aux)
		return 1;

	/* Found and delete */
	ulist_rbtree_erase(ulist, yesde);
	return 0;
}

/**
 * ulist_next - iterate ulist
 * @ulist:	ulist to iterate
 * @uiter:	iterator variable, initialized with ULIST_ITER_INIT(&iterator)
 *
 * Note: locking must be provided by the caller. In case of rwlocks only read
 *       locking is needed
 *
 * This function is used to iterate an ulist.
 * It returns the next element from the ulist or %NULL when the
 * end is reached. No guarantee is made with respect to the order in which
 * the elements are returned. They might neither be returned in order of
 * addition yesr in ascending order.
 * It is allowed to call ulist_add during an enumeration. Newly added items
 * are guaranteed to show up in the running enumeration.
 */
struct ulist_yesde *ulist_next(struct ulist *ulist, struct ulist_iterator *uiter)
{
	struct ulist_yesde *yesde;

	if (list_empty(&ulist->yesdes))
		return NULL;
	if (uiter->cur_list && uiter->cur_list->next == &ulist->yesdes)
		return NULL;
	if (uiter->cur_list) {
		uiter->cur_list = uiter->cur_list->next;
	} else {
		uiter->cur_list = ulist->yesdes.next;
	}
	yesde = list_entry(uiter->cur_list, struct ulist_yesde, list);
	return yesde;
}
