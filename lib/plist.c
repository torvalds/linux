// SPDX-License-Identifier: GPL-2.0-or-later
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
 * Based on simple lists (include/linux/list.h).
 *
 * This file contains the add / del functions which are considered to
 * be too large to inline. See include/linux/plist.h for further
 * information.
 */

#include <linux/bug.h>
#include <linux/plist.h>

#ifdef CONFIG_DEBUG_PLIST

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
	plist_check_list(&head->yesde_list);
}

#else
# define plist_check_head(h)	do { } while (0)
#endif

/**
 * plist_add - add @yesde to @head
 *
 * @yesde:	&struct plist_yesde pointer
 * @head:	&struct plist_head pointer
 */
void plist_add(struct plist_yesde *yesde, struct plist_head *head)
{
	struct plist_yesde *first, *iter, *prev = NULL;
	struct list_head *yesde_next = &head->yesde_list;

	plist_check_head(head);
	WARN_ON(!plist_yesde_empty(yesde));
	WARN_ON(!list_empty(&yesde->prio_list));

	if (plist_head_empty(head))
		goto ins_yesde;

	first = iter = plist_first(head);

	do {
		if (yesde->prio < iter->prio) {
			yesde_next = &iter->yesde_list;
			break;
		}

		prev = iter;
		iter = list_entry(iter->prio_list.next,
				struct plist_yesde, prio_list);
	} while (iter != first);

	if (!prev || prev->prio != yesde->prio)
		list_add_tail(&yesde->prio_list, &iter->prio_list);
ins_yesde:
	list_add_tail(&yesde->yesde_list, yesde_next);

	plist_check_head(head);
}

/**
 * plist_del - Remove a @yesde from plist.
 *
 * @yesde:	&struct plist_yesde pointer - entry to be removed
 * @head:	&struct plist_head pointer - list head
 */
void plist_del(struct plist_yesde *yesde, struct plist_head *head)
{
	plist_check_head(head);

	if (!list_empty(&yesde->prio_list)) {
		if (yesde->yesde_list.next != &head->yesde_list) {
			struct plist_yesde *next;

			next = list_entry(yesde->yesde_list.next,
					struct plist_yesde, yesde_list);

			/* add the next plist_yesde into prio_list */
			if (list_empty(&next->prio_list))
				list_add(&next->prio_list, &yesde->prio_list);
		}
		list_del_init(&yesde->prio_list);
	}

	list_del_init(&yesde->yesde_list);

	plist_check_head(head);
}

/**
 * plist_requeue - Requeue @yesde at end of same-prio entries.
 *
 * This is essentially an optimized plist_del() followed by
 * plist_add().  It moves an entry already in the plist to
 * after any other same-priority entries.
 *
 * @yesde:	&struct plist_yesde pointer - entry to be moved
 * @head:	&struct plist_head pointer - list head
 */
void plist_requeue(struct plist_yesde *yesde, struct plist_head *head)
{
	struct plist_yesde *iter;
	struct list_head *yesde_next = &head->yesde_list;

	plist_check_head(head);
	BUG_ON(plist_head_empty(head));
	BUG_ON(plist_yesde_empty(yesde));

	if (yesde == plist_last(head))
		return;

	iter = plist_next(yesde);

	if (yesde->prio != iter->prio)
		return;

	plist_del(yesde, head);

	plist_for_each_continue(iter, head) {
		if (yesde->prio != iter->prio) {
			yesde_next = &iter->yesde_list;
			break;
		}
	}
	list_add_tail(&yesde->yesde_list, yesde_next);

	plist_check_head(head);
}

#ifdef CONFIG_DEBUG_PLIST
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/module.h>
#include <linux/init.h>

static struct plist_yesde __initdata test_yesde[241];

static void __init plist_test_check(int nr_expect)
{
	struct plist_yesde *first, *prio_pos, *yesde_pos;

	if (plist_head_empty(&test_head)) {
		BUG_ON(nr_expect != 0);
		return;
	}

	prio_pos = first = plist_first(&test_head);
	plist_for_each(yesde_pos, &test_head) {
		if (nr_expect-- < 0)
			break;
		if (yesde_pos == first)
			continue;
		if (yesde_pos->prio == prio_pos->prio) {
			BUG_ON(!list_empty(&yesde_pos->prio_list));
			continue;
		}

		BUG_ON(prio_pos->prio > yesde_pos->prio);
		BUG_ON(prio_pos->prio_list.next != &yesde_pos->prio_list);
		prio_pos = yesde_pos;
	}

	BUG_ON(nr_expect != 0);
	BUG_ON(prio_pos->prio_list.next != &first->prio_list);
}

static void __init plist_test_requeue(struct plist_yesde *yesde)
{
	plist_requeue(yesde, &test_head);

	if (yesde != plist_last(&test_head))
		BUG_ON(yesde->prio == plist_next(yesde)->prio);
}

static int  __init plist_test(void)
{
	int nr_expect = 0, i, loop;
	unsigned int r = local_clock();

	printk(KERN_DEBUG "start plist test\n");
	plist_head_init(&test_head);
	for (i = 0; i < ARRAY_SIZE(test_yesde); i++)
		plist_yesde_init(test_yesde + i, 0);

	for (loop = 0; loop < 1000; loop++) {
		r = r * 193939 % 47629;
		i = r % ARRAY_SIZE(test_yesde);
		if (plist_yesde_empty(test_yesde + i)) {
			r = r * 193939 % 47629;
			test_yesde[i].prio = r % 99;
			plist_add(test_yesde + i, &test_head);
			nr_expect++;
		} else {
			plist_del(test_yesde + i, &test_head);
			nr_expect--;
		}
		plist_test_check(nr_expect);
		if (!plist_yesde_empty(test_yesde + i)) {
			plist_test_requeue(test_yesde + i);
			plist_test_check(nr_expect);
		}
	}

	for (i = 0; i < ARRAY_SIZE(test_yesde); i++) {
		if (plist_yesde_empty(test_yesde + i))
			continue;
		plist_del(test_yesde + i, &test_head);
		nr_expect--;
		plist_test_check(nr_expect);
	}

	printk(KERN_DEBUG "end plist test\n");
	return 0;
}

module_init(plist_test);

#endif
