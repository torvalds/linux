/*
 * Copyright 2006, Red Hat, Inc., Dave Jones
 * Released under the General Public License (GPL).
 *
 * This file contains the linked list validation for DEBUG_LIST.
 */

#include <linux/export.h>
#include <linux/list.h>
#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/rculist.h>

/*
 * Check that the data structures for the list manipulations are reasonably
 * valid. Failures here indicate memory corruption (and possibly an exploit
 * attempt).
 */

bool __list_add_valid(struct list_head *new, struct list_head *prev,
		      struct list_head *next)
{
	if (unlikely(next->prev != prev)) {
		WARN(1, "list_add corruption. next->prev should be prev (%p), but was %p. (next=%p).\n",
			prev, next->prev, next);
		return false;
	}
	if (unlikely(prev->next != next)) {
		WARN(1, "list_add corruption. prev->next should be next (%p), but was %p. (prev=%p).\n",
			next, prev->next, prev);
		return false;
	}
	if (unlikely(new == prev || new == next)) {
		WARN(1, "list_add double add: new=%p, prev=%p, next=%p.\n",
			new, prev, next);
		return false;
	}
	return true;
}
EXPORT_SYMBOL(__list_add_valid);

void __list_del_entry(struct list_head *entry)
{
	struct list_head *prev, *next;

	prev = entry->prev;
	next = entry->next;

	if (WARN(next == LIST_POISON1,
		"list_del corruption, %p->next is LIST_POISON1 (%p)\n",
		entry, LIST_POISON1) ||
	    WARN(prev == LIST_POISON2,
		"list_del corruption, %p->prev is LIST_POISON2 (%p)\n",
		entry, LIST_POISON2) ||
	    WARN(prev->next != entry,
		"list_del corruption. prev->next should be %p, "
		"but was %p\n", entry, prev->next) ||
	    WARN(next->prev != entry,
		"list_del corruption. next->prev should be %p, "
		"but was %p\n", entry, next->prev))
		return;

	__list_del(prev, next);
}
EXPORT_SYMBOL(__list_del_entry);

/**
 * list_del - deletes entry from list.
 * @entry: the element to delete from the list.
 * Note: list_empty on entry does not return true after this, the entry is
 * in an undefined state.
 */
void list_del(struct list_head *entry)
{
	__list_del_entry(entry);
	entry->next = LIST_POISON1;
	entry->prev = LIST_POISON2;
}
EXPORT_SYMBOL(list_del);

/*
 * RCU variants.
 */
void __list_add_rcu(struct list_head *new,
		    struct list_head *prev, struct list_head *next)
{
	WARN(next->prev != prev,
		"list_add_rcu corruption. next->prev should be prev (%p), but was %p. (next=%p).\n",
		prev, next->prev, next);
	WARN(prev->next != next,
		"list_add_rcu corruption. prev->next should be next (%p), but was %p. (prev=%p).\n",
		next, prev->next, prev);
	new->next = next;
	new->prev = prev;
	rcu_assign_pointer(list_next_rcu(prev), new);
	next->prev = new;
}
EXPORT_SYMBOL(__list_add_rcu);
