/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_LIST_SORT_H
#define _LINUX_LIST_SORT_H

#include <linux/types.h>

struct list_head;

__attribute__((nonnull(2,3)))
void list_sort(void *priv, struct list_head *head,
	       int (*cmp)(void *priv, struct list_head *a,
			  struct list_head *b));
#endif
