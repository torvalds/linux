#ifndef _TYPES_H
#define _TYPES_H

#define __rcu
#define __read_mostly

#define BITS_PER_LONG (sizeof(long) * 8)

struct list_head {
	struct list_head *next, *prev;
};

static inline void INIT_LIST_HEAD(struct list_head *list)
{
	list->next = list;
	list->prev = list;
}

typedef struct {
	unsigned int x;
} spinlock_t;

#define uninitialized_var(x) x = x

typedef unsigned gfp_t;
#include <linux/gfp.h>

#endif
