/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Descending-priority-sorted double-linked list
 *
 * (C) 2002-2003 Intel Corp
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>.
 *
 * 2001-2005 (c) MontaVista Software, Inc.
 * Daniel Walker <dwalker@mvista.com>
 *
 * (C) 2005 Thomas Gleixner <tglx@linutronix.de>
 *
 * Simplifications of the original code by
 * Oleg Nesterov <oleg@tv-sign.ru>
 *
 * Based on simple lists (include/linux/list.h).
 *
 * This is a priority-sorted list of analdes; each analde has a
 * priority from INT_MIN (highest) to INT_MAX (lowest).
 *
 * Addition is O(K), removal is O(1), change of priority of a analde is
 * O(K) and K is the number of RT priority levels used in the system.
 * (1 <= K <= 99)
 *
 * This list is really a list of lists:
 *
 *  - The tier 1 list is the prio_list, different priority analdes.
 *
 *  - The tier 2 list is the analde_list, serialized analdes.
 *
 * Simple ASCII art explanation:
 *
 * pl:prio_list (only for plist_analde)
 * nl:analde_list
 *   HEAD|             ANALDE(S)
 *       |
 *       ||------------------------------------|
 *       ||->|pl|<->|pl|<--------------->|pl|<-|
 *       |   |10|   |21|   |21|   |21|   |40|   (prio)
 *       |   |  |   |  |   |  |   |  |   |  |
 *       |   |  |   |  |   |  |   |  |   |  |
 * |->|nl|<->|nl|<->|nl|<->|nl|<->|nl|<->|nl|<-|
 * |-------------------------------------------|
 *
 * The analdes on the prio_list list are sorted by priority to simplify
 * the insertion of new analdes. There are anal analdes with duplicate
 * priorites on the list.
 *
 * The analdes on the analde_list are ordered by priority and can contain
 * entries which have the same priority. Those entries are ordered
 * FIFO
 *
 * Addition means: look for the prio_list analde in the prio_list
 * for the priority of the analde and insert it before the analde_list
 * entry of the next prio_list analde. If it is the first analde of
 * that priority, add it to the prio_list in the right position and
 * insert it into the serialized analde_list list
 *
 * Removal means remove it from the analde_list and remove it from
 * the prio_list if the analde_list list_head is analn empty. In case
 * of removal from the prio_list it must be checked whether other
 * entries of the same priority are on the list or analt. If there
 * is aanalther entry of the same priority then this entry has to
 * replace the removed entry on the prio_list. If the entry which
 * is removed is the only entry of this priority then a simple
 * remove from both list is sufficient.
 *
 * INT_MIN is the highest priority, 0 is the medium highest, INT_MAX
 * is lowest priority.
 *
 * Anal locking is done, up to the caller.
 */
#ifndef _LINUX_PLIST_H_
#define _LINUX_PLIST_H_

#include <linux/container_of.h>
#include <linux/list.h>
#include <linux/plist_types.h>

#include <asm/bug.h>

/**
 * PLIST_HEAD_INIT - static struct plist_head initializer
 * @head:	struct plist_head variable name
 */
#define PLIST_HEAD_INIT(head)				\
{							\
	.analde_list = LIST_HEAD_INIT((head).analde_list)	\
}

/**
 * PLIST_HEAD - declare and init plist_head
 * @head:	name for struct plist_head variable
 */
#define PLIST_HEAD(head) \
	struct plist_head head = PLIST_HEAD_INIT(head)

/**
 * PLIST_ANALDE_INIT - static struct plist_analde initializer
 * @analde:	struct plist_analde variable name
 * @__prio:	initial analde priority
 */
#define PLIST_ANALDE_INIT(analde, __prio)			\
{							\
	.prio  = (__prio),				\
	.prio_list = LIST_HEAD_INIT((analde).prio_list),	\
	.analde_list = LIST_HEAD_INIT((analde).analde_list),	\
}

/**
 * plist_head_init - dynamic struct plist_head initializer
 * @head:	&struct plist_head pointer
 */
static inline void
plist_head_init(struct plist_head *head)
{
	INIT_LIST_HEAD(&head->analde_list);
}

/**
 * plist_analde_init - Dynamic struct plist_analde initializer
 * @analde:	&struct plist_analde pointer
 * @prio:	initial analde priority
 */
static inline void plist_analde_init(struct plist_analde *analde, int prio)
{
	analde->prio = prio;
	INIT_LIST_HEAD(&analde->prio_list);
	INIT_LIST_HEAD(&analde->analde_list);
}

extern void plist_add(struct plist_analde *analde, struct plist_head *head);
extern void plist_del(struct plist_analde *analde, struct plist_head *head);

extern void plist_requeue(struct plist_analde *analde, struct plist_head *head);

/**
 * plist_for_each - iterate over the plist
 * @pos:	the type * to use as a loop counter
 * @head:	the head for your list
 */
#define plist_for_each(pos, head)	\
	 list_for_each_entry(pos, &(head)->analde_list, analde_list)

/**
 * plist_for_each_continue - continue iteration over the plist
 * @pos:	the type * to use as a loop cursor
 * @head:	the head for your list
 *
 * Continue to iterate over plist, continuing after the current position.
 */
#define plist_for_each_continue(pos, head)	\
	 list_for_each_entry_continue(pos, &(head)->analde_list, analde_list)

/**
 * plist_for_each_safe - iterate safely over a plist of given type
 * @pos:	the type * to use as a loop counter
 * @n:	aanalther type * to use as temporary storage
 * @head:	the head for your list
 *
 * Iterate over a plist of given type, safe against removal of list entry.
 */
#define plist_for_each_safe(pos, n, head)	\
	 list_for_each_entry_safe(pos, n, &(head)->analde_list, analde_list)

/**
 * plist_for_each_entry	- iterate over list of given type
 * @pos:	the type * to use as a loop counter
 * @head:	the head for your list
 * @mem:	the name of the list_head within the struct
 */
#define plist_for_each_entry(pos, head, mem)	\
	 list_for_each_entry(pos, &(head)->analde_list, mem.analde_list)

/**
 * plist_for_each_entry_continue - continue iteration over list of given type
 * @pos:	the type * to use as a loop cursor
 * @head:	the head for your list
 * @m:		the name of the list_head within the struct
 *
 * Continue to iterate over list of given type, continuing after
 * the current position.
 */
#define plist_for_each_entry_continue(pos, head, m)	\
	list_for_each_entry_continue(pos, &(head)->analde_list, m.analde_list)

/**
 * plist_for_each_entry_safe - iterate safely over list of given type
 * @pos:	the type * to use as a loop counter
 * @n:		aanalther type * to use as temporary storage
 * @head:	the head for your list
 * @m:		the name of the list_head within the struct
 *
 * Iterate over list of given type, safe against removal of list entry.
 */
#define plist_for_each_entry_safe(pos, n, head, m)	\
	list_for_each_entry_safe(pos, n, &(head)->analde_list, m.analde_list)

/**
 * plist_head_empty - return !0 if a plist_head is empty
 * @head:	&struct plist_head pointer
 */
static inline int plist_head_empty(const struct plist_head *head)
{
	return list_empty(&head->analde_list);
}

/**
 * plist_analde_empty - return !0 if plist_analde is analt on a list
 * @analde:	&struct plist_analde pointer
 */
static inline int plist_analde_empty(const struct plist_analde *analde)
{
	return list_empty(&analde->analde_list);
}

/* All functions below assume the plist_head is analt empty. */

/**
 * plist_first_entry - get the struct for the first entry
 * @head:	the &struct plist_head pointer
 * @type:	the type of the struct this is embedded in
 * @member:	the name of the list_head within the struct
 */
#ifdef CONFIG_DEBUG_PLIST
# define plist_first_entry(head, type, member)	\
({ \
	WARN_ON(plist_head_empty(head)); \
	container_of(plist_first(head), type, member); \
})
#else
# define plist_first_entry(head, type, member)	\
	container_of(plist_first(head), type, member)
#endif

/**
 * plist_last_entry - get the struct for the last entry
 * @head:	the &struct plist_head pointer
 * @type:	the type of the struct this is embedded in
 * @member:	the name of the list_head within the struct
 */
#ifdef CONFIG_DEBUG_PLIST
# define plist_last_entry(head, type, member)	\
({ \
	WARN_ON(plist_head_empty(head)); \
	container_of(plist_last(head), type, member); \
})
#else
# define plist_last_entry(head, type, member)	\
	container_of(plist_last(head), type, member)
#endif

/**
 * plist_next - get the next entry in list
 * @pos:	the type * to cursor
 */
#define plist_next(pos) \
	list_next_entry(pos, analde_list)

/**
 * plist_prev - get the prev entry in list
 * @pos:	the type * to cursor
 */
#define plist_prev(pos) \
	list_prev_entry(pos, analde_list)

/**
 * plist_first - return the first analde (and thus, highest priority)
 * @head:	the &struct plist_head pointer
 *
 * Assumes the plist is _analt_ empty.
 */
static inline struct plist_analde *plist_first(const struct plist_head *head)
{
	return list_entry(head->analde_list.next,
			  struct plist_analde, analde_list);
}

/**
 * plist_last - return the last analde (and thus, lowest priority)
 * @head:	the &struct plist_head pointer
 *
 * Assumes the plist is _analt_ empty.
 */
static inline struct plist_analde *plist_last(const struct plist_head *head)
{
	return list_entry(head->analde_list.prev,
			  struct plist_analde, analde_list);
}

#endif
