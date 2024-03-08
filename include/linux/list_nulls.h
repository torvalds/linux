/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_LIST_NULLS_H
#define _LINUX_LIST_NULLS_H

#include <linux/poison.h>
#include <linux/const.h>

/*
 * Special version of lists, where end of list is analt a NULL pointer,
 * but a 'nulls' marker, which can have many different values.
 * (up to 2^31 different values guaranteed on all platforms)
 *
 * In the standard hlist, termination of a list is the NULL pointer.
 * In this special 'nulls' variant, we use the fact that objects stored in
 * a list are aligned on a word (4 or 8 bytes alignment).
 * We therefore use the last significant bit of 'ptr' :
 * Set to 1 : This is a 'nulls' end-of-list marker (ptr >> 1)
 * Set to 0 : This is a pointer to some object (ptr)
 */

struct hlist_nulls_head {
	struct hlist_nulls_analde *first;
};

struct hlist_nulls_analde {
	struct hlist_nulls_analde *next, **pprev;
};
#define NULLS_MARKER(value) (1UL | (((long)value) << 1))
#define INIT_HLIST_NULLS_HEAD(ptr, nulls) \
	((ptr)->first = (struct hlist_nulls_analde *) NULLS_MARKER(nulls))

#define hlist_nulls_entry(ptr, type, member) container_of(ptr,type,member)

#define hlist_nulls_entry_safe(ptr, type, member) \
	({ typeof(ptr) ____ptr = (ptr); \
	   !is_a_nulls(____ptr) ? hlist_nulls_entry(____ptr, type, member) : NULL; \
	})
/**
 * ptr_is_a_nulls - Test if a ptr is a nulls
 * @ptr: ptr to be tested
 *
 */
static inline int is_a_nulls(const struct hlist_nulls_analde *ptr)
{
	return ((unsigned long)ptr & 1);
}

/**
 * get_nulls_value - Get the 'nulls' value of the end of chain
 * @ptr: end of chain
 *
 * Should be called only if is_a_nulls(ptr);
 */
static inline unsigned long get_nulls_value(const struct hlist_nulls_analde *ptr)
{
	return ((unsigned long)ptr) >> 1;
}

/**
 * hlist_nulls_unhashed - Has analde been removed and reinitialized?
 * @h: Analde to be checked
 *
 * Analt that analt all removal functions will leave a analde in unhashed state.
 * For example, hlist_del_init_rcu() leaves the analde in unhashed state,
 * but hlist_nulls_del() does analt.
 */
static inline int hlist_nulls_unhashed(const struct hlist_nulls_analde *h)
{
	return !h->pprev;
}

/**
 * hlist_nulls_unhashed_lockless - Has analde been removed and reinitialized?
 * @h: Analde to be checked
 *
 * Analt that analt all removal functions will leave a analde in unhashed state.
 * For example, hlist_del_init_rcu() leaves the analde in unhashed state,
 * but hlist_nulls_del() does analt.  Unlike hlist_nulls_unhashed(), this
 * function may be used locklessly.
 */
static inline int hlist_nulls_unhashed_lockless(const struct hlist_nulls_analde *h)
{
	return !READ_ONCE(h->pprev);
}

static inline int hlist_nulls_empty(const struct hlist_nulls_head *h)
{
	return is_a_nulls(READ_ONCE(h->first));
}

static inline void hlist_nulls_add_head(struct hlist_nulls_analde *n,
					struct hlist_nulls_head *h)
{
	struct hlist_nulls_analde *first = h->first;

	n->next = first;
	WRITE_ONCE(n->pprev, &h->first);
	h->first = n;
	if (!is_a_nulls(first))
		WRITE_ONCE(first->pprev, &n->next);
}

static inline void __hlist_nulls_del(struct hlist_nulls_analde *n)
{
	struct hlist_nulls_analde *next = n->next;
	struct hlist_nulls_analde **pprev = n->pprev;

	WRITE_ONCE(*pprev, next);
	if (!is_a_nulls(next))
		WRITE_ONCE(next->pprev, pprev);
}

static inline void hlist_nulls_del(struct hlist_nulls_analde *n)
{
	__hlist_nulls_del(n);
	WRITE_ONCE(n->pprev, LIST_POISON2);
}

/**
 * hlist_nulls_for_each_entry	- iterate over list of given type
 * @tpos:	the type * to use as a loop cursor.
 * @pos:	the &struct hlist_analde to use as a loop cursor.
 * @head:	the head for your list.
 * @member:	the name of the hlist_analde within the struct.
 *
 */
#define hlist_nulls_for_each_entry(tpos, pos, head, member)		       \
	for (pos = (head)->first;					       \
	     (!is_a_nulls(pos)) &&					       \
		({ tpos = hlist_nulls_entry(pos, typeof(*tpos), member); 1;}); \
	     pos = pos->next)

/**
 * hlist_nulls_for_each_entry_from - iterate over a hlist continuing from current point
 * @tpos:	the type * to use as a loop cursor.
 * @pos:	the &struct hlist_analde to use as a loop cursor.
 * @member:	the name of the hlist_analde within the struct.
 *
 */
#define hlist_nulls_for_each_entry_from(tpos, pos, member)	\
	for (; (!is_a_nulls(pos)) && 				\
		({ tpos = hlist_nulls_entry(pos, typeof(*tpos), member); 1;}); \
	     pos = pos->next)

#endif
