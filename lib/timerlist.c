/*
 *  Generic Timer-list
 *
 *  Manages a simple list of timers, ordered by expiration time.
 *  Uses rbtrees for quick list adds and expiration.
 *
 *  NOTE: All of the following functions need to be serialized
 *  to avoid races. No locking is done by this libary code.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/timerlist.h>
#include <linux/rbtree.h>

/**
 * timerlist_add - Adds timer to timerlist.
 *
 * @head: head of timerlist
 * @node: timer node to be added
 *
 * Adds the timer node to the timerlist, sorted by the
 * node's expires value.
 */
void timerlist_add(struct timerlist_head *head, struct timerlist_node *node)
{
	struct rb_node **p = &head->head.rb_node;
	struct rb_node *parent = NULL;
	struct timerlist_node  *ptr;

	/* Make sure we don't add nodes that are already added */
	WARN_ON_ONCE(!RB_EMPTY_NODE(&node->node));

	while (*p) {
		parent = *p;
		ptr = rb_entry(parent, struct timerlist_node, node);
		if (node->expires.tv64 < ptr->expires.tv64)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}
	rb_link_node(&node->node, parent, p);
	rb_insert_color(&node->node, &head->head);

	if (!head->next || node->expires.tv64 < head->next->expires.tv64)
		head->next = node;
}

/**
 * timerlist_del - Removes a timer from the timerlist.
 *
 * @head: head of timerlist
 * @node: timer node to be removed
 *
 * Removes the timer node from the timerlist.
 */
void timerlist_del(struct timerlist_head *head, struct timerlist_node *node)
{
	WARN_ON_ONCE(RB_EMPTY_NODE(&node->node));

	/* update next pointer */
	if (head->next == node) {
		struct rb_node *rbn = rb_next(&node->node);

		head->next = rbn ?
			rb_entry(rbn, struct timerlist_node, node) : NULL;
	}
	rb_erase(&node->node, &head->head);
	RB_CLEAR_NODE(&node->node);
}


/**
 * timerlist_getnext - Returns the timer with the earlies expiration time
 *
 * @head: head of timerlist
 *
 * Returns a pointer to the timer node that has the
 * earliest expiration time.
 */
struct timerlist_node *timerlist_getnext(struct timerlist_head *head)
{
	return head->next;
}


/**
 * timerlist_iterate_next - Returns the timer after the provided timer
 *
 * @node: Pointer to a timer.
 *
 * Provides the timer that is after the given node. This is used, when
 * necessary, to iterate through the list of timers in a timer list
 * without modifying the list.
 */
struct timerlist_node *timerlist_iterate_next(struct timerlist_node *node)
{
	struct rb_node *next;

	if (!node)
		return NULL;
	next = rb_next(&node->node);
	if (!next)
		return NULL;
	return container_of(next, struct timerlist_node, node);
}
