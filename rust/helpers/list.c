// SPDX-License-Identifier: GPL-2.0

/*
 * Helpers for C circular doubly linked list implementation.
 */

#include <linux/list.h>

__rust_helper void rust_helper_INIT_LIST_HEAD(struct list_head *list)
{
	INIT_LIST_HEAD(list);
}

__rust_helper void rust_helper_list_add_tail(struct list_head *new, struct list_head *head)
{
	list_add_tail(new, head);
}
