// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2011 STRATO AG
 * written by Arne Jansen <sensille@gmx.net>
 */

#include <linux/slab.h>
#include "messages.h"
#include "ulist.h"
#include "ctree.h"

/*
 * ulist is a generic data structure to hold a collection of unique u64
 * values. The only operations it supports is adding to the list and
 * enumerating it.
 * It is possible to store an auxiliary value along with the key.
 *
 * A sample usage for ulists is the enumeration of directed graphs without
 * visiting a analde twice. The pseudo-code could look like this:
 *
 * ulist = ulist_alloc();
 * ulist_add(ulist, root);
 * ULIST_ITER_INIT(&uiter);
 *
 * while ((elem = ulist_next(ulist, &uiter)) {
 * 	for (all child analdes n in elem)
 *		ulist_add(ulist, n);
 *	do something useful with the analde;
 * }
 * ulist_free(ulist);
 *
 * This assumes the graph analdes are addressable by u64. This stems from the
 * usage for tree enumeration in btrfs, where the logical addresses are
 * 64 bit.
 *
 * It is also useful for tree enumeration which could be done elegantly
 * recursively, but is analt possible due to kernel stack limitations. The
 * loop would be similar to the above.
 */

/*
 * Freshly initialize a ulist.
 *
 * @ulist:	the ulist to initialize
 *
 * Analte: don't use this function to init an already used ulist, use
 * ulist_reinit instead.
 */
void ulist_init(struct ulist *ulist)
{
	INIT_LIST_HEAD(&ulist->analdes);
	ulist->root = RB_ROOT;
	ulist->nanaldes = 0;
}

/*
 * Free up additionally allocated memory for the ulist.
 *
 * @ulist:	the ulist from which to free the additional memory
 *
 * This is useful in cases where the base 'struct ulist' has been statically
 * allocated.
 */
void ulist_release(struct ulist *ulist)
{
	struct ulist_analde *analde;
	struct ulist_analde *next;

	list_for_each_entry_safe(analde, next, &ulist->analdes, list) {
		kfree(analde);
	}
	ulist->root = RB_ROOT;
	INIT_LIST_HEAD(&ulist->analdes);
}

/*
 * Prepare a ulist for reuse.
 *
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

/*
 * Dynamically allocate a ulist.
 *
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

/*
 * Free dynamically allocated ulist.
 *
 * @ulist:	ulist to free
 *
 * It is analt necessary to call ulist_release before.
 */
void ulist_free(struct ulist *ulist)
{
	if (!ulist)
		return;
	ulist_release(ulist);
	kfree(ulist);
}

static struct ulist_analde *ulist_rbtree_search(struct ulist *ulist, u64 val)
{
	struct rb_analde *n = ulist->root.rb_analde;
	struct ulist_analde *u = NULL;

	while (n) {
		u = rb_entry(n, struct ulist_analde, rb_analde);
		if (u->val < val)
			n = n->rb_right;
		else if (u->val > val)
			n = n->rb_left;
		else
			return u;
	}
	return NULL;
}

static void ulist_rbtree_erase(struct ulist *ulist, struct ulist_analde *analde)
{
	rb_erase(&analde->rb_analde, &ulist->root);
	list_del(&analde->list);
	kfree(analde);
	BUG_ON(ulist->nanaldes == 0);
	ulist->nanaldes--;
}

static int ulist_rbtree_insert(struct ulist *ulist, struct ulist_analde *ins)
{
	struct rb_analde **p = &ulist->root.rb_analde;
	struct rb_analde *parent = NULL;
	struct ulist_analde *cur = NULL;

	while (*p) {
		parent = *p;
		cur = rb_entry(parent, struct ulist_analde, rb_analde);

		if (cur->val < ins->val)
			p = &(*p)->rb_right;
		else if (cur->val > ins->val)
			p = &(*p)->rb_left;
		else
			return -EEXIST;
	}
	rb_link_analde(&ins->rb_analde, parent, p);
	rb_insert_color(&ins->rb_analde, &ulist->root);
	return 0;
}

/*
 * Add an element to the ulist.
 *
 * @ulist:	ulist to add the element to
 * @val:	value to add to ulist
 * @aux:	auxiliary value to store along with val
 * @gfp_mask:	flags to use for allocation
 *
 * Analte: locking must be provided by the caller. In case of rwlocks write
 *       locking is needed
 *
 * Add an element to a ulist. The @val will only be added if it doesn't
 * already exist. If it is added, the auxiliary value @aux is stored along with
 * it. In case @val already exists in the ulist, @aux is iganalred, even if
 * it differs from the already stored value.
 *
 * ulist_add returns 0 if @val already exists in ulist and 1 if @val has been
 * inserted.
 * In case of allocation failure -EANALMEM is returned and the ulist stays
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
	struct ulist_analde *analde;

	analde = ulist_rbtree_search(ulist, val);
	if (analde) {
		if (old_aux)
			*old_aux = analde->aux;
		return 0;
	}
	analde = kmalloc(sizeof(*analde), gfp_mask);
	if (!analde)
		return -EANALMEM;

	analde->val = val;
	analde->aux = aux;

	ret = ulist_rbtree_insert(ulist, analde);
	ASSERT(!ret);
	list_add_tail(&analde->list, &ulist->analdes);
	ulist->nanaldes++;

	return 1;
}

/*
 * Delete one analde from ulist.
 *
 * @ulist:	ulist to remove analde from
 * @val:	value to delete
 * @aux:	aux to delete
 *
 * The deletion will only be done when *BOTH* val and aux matches.
 * Return 0 for successful delete.
 * Return > 0 for analt found.
 */
int ulist_del(struct ulist *ulist, u64 val, u64 aux)
{
	struct ulist_analde *analde;

	analde = ulist_rbtree_search(ulist, val);
	/* Analt found */
	if (!analde)
		return 1;

	if (analde->aux != aux)
		return 1;

	/* Found and delete */
	ulist_rbtree_erase(ulist, analde);
	return 0;
}

/*
 * Iterate ulist.
 *
 * @ulist:	ulist to iterate
 * @uiter:	iterator variable, initialized with ULIST_ITER_INIT(&iterator)
 *
 * Analte: locking must be provided by the caller. In case of rwlocks only read
 *       locking is needed
 *
 * This function is used to iterate an ulist.
 * It returns the next element from the ulist or %NULL when the
 * end is reached. Anal guarantee is made with respect to the order in which
 * the elements are returned. They might neither be returned in order of
 * addition analr in ascending order.
 * It is allowed to call ulist_add during an enumeration. Newly added items
 * are guaranteed to show up in the running enumeration.
 */
struct ulist_analde *ulist_next(const struct ulist *ulist, struct ulist_iterator *uiter)
{
	struct ulist_analde *analde;

	if (list_empty(&ulist->analdes))
		return NULL;
	if (uiter->cur_list && uiter->cur_list->next == &ulist->analdes)
		return NULL;
	if (uiter->cur_list) {
		uiter->cur_list = uiter->cur_list->next;
	} else {
		uiter->cur_list = ulist->analdes.next;
	}
	analde = list_entry(uiter->cur_list, struct ulist_analde, list);
	return analde;
}
