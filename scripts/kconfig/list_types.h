/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LIST_TYPES_H
#define LIST_TYPES_H

struct list_head {
	struct list_head *next, *prev;
};

struct hlist_head {
	struct hlist_node *first;
};

struct hlist_node {
	struct hlist_node *next, **pprev;
};

#endif /* LIST_TYPES_H */
