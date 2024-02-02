/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LIST_TYPES_H
#define LIST_TYPES_H

struct list_head {
	struct list_head *next, *prev;
};

#endif /* LIST_TYPES_H */
