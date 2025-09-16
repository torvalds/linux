#ifndef _LINUX_FAST_LIST_H
#define _LINUX_FAST_LIST_H

#include <linux/generic-radix-tree.h>
#include <linux/idr.h>
#include <linux/percpu.h>

struct fast_list_pcpu;

struct fast_list {
	GENRADIX(void *)	items;
	struct ida		slots_allocated;;
	struct fast_list_pcpu __percpu
				*buffer;
};

static inline void *fast_list_iter_peek(struct genradix_iter *iter,
					struct fast_list *list)
{
	void **p;
	while ((p = genradix_iter_peek(iter, &list->items)) && !*p)
		genradix_iter_advance(iter, &list->items);

	return p ? *p : NULL;
}

#define fast_list_for_each_from(_list, _iter, _i, _start)		\
	for (_iter = genradix_iter_init(&(_list)->items, _start);	\
	     (_i = fast_list_iter_peek(&(_iter), _list)) != NULL;	\
	     genradix_iter_advance(&(_iter), &(_list)->items))

#define fast_list_for_each(_list, _iter, _i)				\
	fast_list_for_each_from(_list, _iter, _i, 0)

int fast_list_get_idx(struct fast_list *l);
int fast_list_add(struct fast_list *l, void *item);
void fast_list_remove(struct fast_list *l, unsigned idx);
void fast_list_exit(struct fast_list *l);
int fast_list_init(struct fast_list *l);

#endif /* _LINUX_FAST_LIST_H */
