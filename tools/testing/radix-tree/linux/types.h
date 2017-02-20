#ifndef _TYPES_H
#define _TYPES_H

#include "../../include/linux/types.h"

#define __rcu
#define __read_mostly

static inline void INIT_LIST_HEAD(struct list_head *list)
{
	list->next = list;
	list->prev = list;
}

typedef struct {
	unsigned int x;
} spinlock_t;

#define uninitialized_var(x) x = x

#include <linux/gfp.h>

#endif
