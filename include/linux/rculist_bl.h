/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_RCULIST_BL_H
#define _LINUX_RCULIST_BL_H

/*
 * RCU-protected bl list version. See include/linux/list_bl.h.
 */
#include <linux/list_bl.h>
#include <linux/rcupdate.h>

static inline void hlist_bl_set_first_rcu(struct hlist_bl_head *h,
					struct hlist_bl_node *n)
{
	LIST_BL_BUG_ON((unsigned long)n & LIST_BL_LOCKMASK);
	LIST_BL_BUG_ON(((unsigned long)h->first & LIST_BL_LOCKMASK) !=
							LIST_BL_LOCKMASK);
	rcu_assign_pointer(h->first,
		(struct hlist_bl_node *)((unsigned long)n | LIST_BL_LOCKMASK));
}

static inline struct hlist_bl_node *hlist_bl_first_rcu(struct hlist_bl_head *h)
{
	return (struct hlist_bl_node *)
		((unsigned long)rcu_dereference_check(h->first, hlist_bl_is_locked(h)) & ~LIST_BL_LOCKMASK);
}

/**
 * hlist_bl_del_init_rcu - deletes entry from hash list with re-initialization
 * @n: the element to delete from the hash list.
 *
 * Note: hlist_bl_unhashed() on the node returns true after this. It is
 * useful for RCU based read lockfree traversal if the writer side
 * must know if the list entry is still hashed or already unhashed.
 *
 * In particular, it means that we can not poison the forward pointers
 * that may still be used for walking the hash list and we can only
 * zero the pprev pointer so list_unhashed() will return true after
 * this.
 *
 * The caller must take whatever precautions are necessary (such as
 * holding appropriate locks) to avoid racing with another
 * list-mutation primitive, such as hlist_bl_add_head_rcu() or
 * hlist_bl_del_rcu(), running on this same list.  However, it is
 * perfectly legal to run concurrently with the _rcu list-traversal
 * primitives, such as hlist_bl_for_each_entry_rcu().
 */
static inline void hlist_bl_del_init_rcu(struct hlist_bl_node *n)
{
	if (!hlist_bl_unhashed(n)) {
		__hlist_bl_del(n);
		n->pprev = NULL;
	}
}

/**
 * hlist_bl_del_rcu - deletes entry from hash list without re-initialization
 * @n: the element to delete from the hash list.
 *
 * Note: hlist_bl_unhashed() on entry does not return true after this,
 * the entry is in an undefined state. It is useful for RCU based
 * lockfree traversal.
 *
 * In particular, it means that we can not poison the forward
 * pointers that may still be used for walking the hash list.
 *
 * The caller must take whatever precautions are necessary
 * (such as holding appropriate locks) to avoid racing
 * with another list-mutation primitive, such as hlist_bl_add_head_rcu()
 * or hlist_bl_del_rcu(), running on this same list.
 * However, it is perfectly legal to run concurrently with
 * the _rcu list-traversal primitives, such as
 * hlist_bl_for_each_entry().
 */
static inline void hlist_bl_del_rcu(struct hlist_bl_node *n)
{
	__hlist_bl_del(n);
	n->pprev = LIST_POISON2;
}

/**
 * hlist_bl_add_head_rcu
 * @n: the element to add to the hash list.
 * @h: the list to add to.
 *
 * Description:
 * Adds the specified element to the specified hlist_bl,
 * while permitting racing traversals.
 *
 * The caller must take whatever precautions are necessary
 * (such as holding appropriate locks) to avoid racing
 * with another list-mutation primitive, such as hlist_bl_add_head_rcu()
 * or hlist_bl_del_rcu(), running on this same list.
 * However, it is perfectly legal to run concurrently with
 * the _rcu list-traversal primitives, such as
 * hlist_bl_for_each_entry_rcu(), used to prevent memory-consistency
 * problems on Alpha CPUs.  Regardless of the type of CPU, the
 * list-traversal primitive must be guarded by rcu_read_lock().
 */
static inline void hlist_bl_add_head_rcu(struct hlist_bl_node *n,
					struct hlist_bl_head *h)
{
	struct hlist_bl_node *first;

	/* don't need hlist_bl_first_rcu because we're under lock */
	first = hlist_bl_first(h);

	n->next = first;
	if (first)
		first->pprev = &n->next;
	n->pprev = &h->first;

	/* need _rcu because we can have concurrent lock free readers */
	hlist_bl_set_first_rcu(h, n);
}
/**
 * hlist_bl_for_each_entry_rcu - iterate over rcu list of given type
 * @tpos:	the type * to use as a loop cursor.
 * @pos:	the &struct hlist_bl_node to use as a loop cursor.
 * @head:	the head for your list.
 * @member:	the name of the hlist_bl_node within the struct.
 *
 */
#define hlist_bl_for_each_entry_rcu(tpos, pos, head, member)		\
	for (pos = hlist_bl_first_rcu(head);				\
		pos &&							\
		({ tpos = hlist_bl_entry(pos, typeof(*tpos), member); 1; }); \
		pos = rcu_dereference_raw(pos->next))

#endif
