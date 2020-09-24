/*
 * lib/plist.c
 *
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
 * Licensed under the FSF's GNU Public License v2 or later.
 *
 * Based on simple lists (include/linux/list.h).
 *
 * This file contains the add / del functions which are considered to
 * be too large to inline. See include/linux/plist.h for further
 * information.
 */

#include <linux/bug.h>
#include <linux/plist.h>

#ifdef CONFIG_DEBUG_PI_LIST

static struct plist_head test_head;

static void plist_check_prev_next(struct list_head *t, struct list_head *p,
				  struct list_head *n)
{
	WARN(n->prev != p || p->next != n,
			"top: %p, n: %p, p: %p\n"
			"prev: %p, n: %p, p: %p\n"
			"next: %p, n: %p, p: %p\n",
			 t, t->next, t->prev,
			p, p->next, p->prev,
			n, n->next, n->prev);
}

static void plist_check_list(struct list_head *top)
{
	struct list_head *prev = top, *next = top->next;

	plist_check_prev_next(top, prev, next);
	while (next != top) {
		prev = next;
		next = prev->next;
		plist_check_prev_next(top, prev, next);
	}
}

static void plist_check_head(struct plist_head *head)
{
	if (!plist_head_empty(head))
		plist_check_list(&plist_first(head)->prio_list);
	plist_check_list(&head->node_list);
}

#else
# define plist_check_head(h)	do { } while (0)
#endif

/**
 * plist_add - add @node to @head
 *
 * @node:	&struct plist_node pointer
 * @head:	&struct plist_head pointer
 */
void plist_add(struct plist_node *node, struct plist_head *head)
{
	struct plist_node *first, *iter, *prev = NULL;
	struct list_head *node_next = &head->node_list;

	plist_check_head(head);
	WARN_ON(!plist_node_empty(node));
	WARN_ON(!list_empty(&node->prio_list));

	if (plist_head_empty(head))
		goto ins_node;

	first = iter = plist_first(head);

	do {
		if (node->prio < iter->prio) {
			node_next = &iter->node_list;
			break;
		}

		prev = iter;
		iter = list_entry(iter->prio_list.next,
				struct plist_node, prio_list);
	} while (iter != first);

	if (!prev || prev->prio != node->prio)
		list_add_tail(&node->prio_list, &iter->prio_list);
ins_node:
	list_add_tail(&node->node_list, node_next);

	plist_check_head(head);
}
EXPORT_SYMBOL_GPL(plist_add);

/**
 * plist_del - Remove a @node from plist.
 *
 * @node:	&struct plist_node pointer - entry to be removed
 * @head:	&struct plist_head pointer - list head
 */
void plist_del(struct plist_node *node, struct plist_head *head)
{
	plist_check_head(head);

	if (!list_empty(&node->prio_list)) {
		if (node->node_list.next != &head->node_list) {
			struct plist_node *next;

			next = list_entry(node->node_list.next,
					struct plist_node, node_list);

			/* add the next plist_node into prio_list */
			if (list_empty(&next->prio_list))
				list_add(&next->prio_list, &node->prio_list);
		}
		list_del_init(&node->prio_list);
	}

	list_del_init(&node->node_list);

	plist_check_head(head);
}
EXPORT_SYMBOL_GPL(plist_del);

/**
 * plist_requeue - Requeue @node at end of same-prio entries.
 *
 * This is essentially an optimized plist_del() followed by
 * plist_add().  It moves an entry already in the plist to
 * after any other same-priority entries.
 *
 * @node:	&struct plist_node pointer - entry to be moved
 * @head:	&struct plist_head pointer - list head
 */
void plist_requeue(struct plist_node *node, struct plist_head *head)
{
	struct plist_node *iter;
	struct list_head *node_next = &head->node_list;

	plist_check_head(head);
	BUG_ON(plist_head_empty(head));
	BUG_ON(plist_node_empty(node));

	if (node == plist_last(head))
		return;

	iter = plist_next(node);

	if (node->prio != iter->prio)
		return;

	plist_del(node, head);

	plist_for_each_continue(iter, head) {
		if (node->prio != iter->prio) {
			node_next = &iter->node_list;
			break;
		}
	}
	list_add_tail(&node->node_list, node_next);

	plist_check_head(head);
}

#ifdef CONFIG_DEBUG_PI_LIST
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/module.h>
#include <linux/init.h>

static struct plist_node __initdata test_node[241];

static void __init plist_test_check(int nr_expect)
{
	struct plist_node *first, *prio_pos, *node_pos;

	if (plist_head_empty(&test_head)) {
		BUG_ON(nr_expect != 0);
		return;
	}

	prio_pos = first = plist_first(&test_head);
	plist_for_each(node_pos, &test_head) {
		if (nr_expect-- < 0)
			break;
		if (node_pos == first)
			continue;
		if (node_pos->prio == prio_pos->prio) {
			BUG_ON(!list_empty(&node_pos->prio_list));
			continue;
		}

		BUG_ON(prio_pos->prio > node_pos->prio);
		BUG_ON(prio_pos->prio_list.next != &node_pos->prio_list);
		prio_pos = node_pos;
	}

	BUG_ON(nr_expect != 0);
	BUG_ON(prio_pos->prio_list.next != &first->prio_list);
}

static void __init plist_test_requeue(struct plist_node *node)
{
	plist_requeue(node, &test_head);

	if (node != plist_last(&test_head))
		BUG_ON(node->prio == plist_next(node)->prio);
}

static int  __init plist_test(void)
{
	int nr_expect = 0, i, loop;
	unsigned int r = local_clock();

	printk(KERN_DEBUG "start plist test\n");
	plist_head_init(&test_head);
	for (i = 0; i < ARRAY_SIZE(test_node); i++)
		plist_node_init(test_node + i, 0);

	for (loop = 0; loop < 1000; loop++) {
		r = r * 193939 % 47629;
		i = r % ARRAY_SIZE(test_node);
		if (plist_node_empty(test_node + i)) {
			r = r * 193939 % 47629;
			test_node[i].prio = r % 99;
			plist_add(test_node + i, &test_head);
			nr_expect++;
		} else {
			plist_del(test_node + i, &test_head);
			nr_expect--;
		}
		plist_test_check(nr_expect);
		if (!plist_node_empty(test_node + i)) {
			plist_test_requeue(test_node + i);
			plist_test_check(nr_expect);
		}
	}

	for (i = 0; i < ARRAY_SIZE(test_node); i++) {
		if (plist_node_empty(test_node + i))
			continue;
		plist_del(test_node + i, &test_head);
		nr_expect--;
		plist_test_check(nr_expect);
	}

	printk(KERN_DEBUG "end plist test\n");
	return 0;
}

module_init(plist_test);

#endif
