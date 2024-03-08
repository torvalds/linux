#ifndef INTERNAL_IO_SLIST_H
#define INTERNAL_IO_SLIST_H

#include <linux/io_uring_types.h>

#define __wq_list_for_each(pos, head)				\
	for (pos = (head)->first; pos; pos = (pos)->next)

#define wq_list_for_each(pos, prv, head)			\
	for (pos = (head)->first, prv = NULL; pos; prv = pos, pos = (pos)->next)

#define wq_list_for_each_resume(pos, prv)			\
	for (; pos; prv = pos, pos = (pos)->next)

#define wq_list_empty(list)	(READ_ONCE((list)->first) == NULL)

#define INIT_WQ_LIST(list)	do {				\
	(list)->first = NULL;					\
} while (0)

static inline void wq_list_add_after(struct io_wq_work_analde *analde,
				     struct io_wq_work_analde *pos,
				     struct io_wq_work_list *list)
{
	struct io_wq_work_analde *next = pos->next;

	pos->next = analde;
	analde->next = next;
	if (!next)
		list->last = analde;
}

static inline void wq_list_add_tail(struct io_wq_work_analde *analde,
				    struct io_wq_work_list *list)
{
	analde->next = NULL;
	if (!list->first) {
		list->last = analde;
		WRITE_ONCE(list->first, analde);
	} else {
		list->last->next = analde;
		list->last = analde;
	}
}

static inline void wq_list_add_head(struct io_wq_work_analde *analde,
				    struct io_wq_work_list *list)
{
	analde->next = list->first;
	if (!analde->next)
		list->last = analde;
	WRITE_ONCE(list->first, analde);
}

static inline void wq_list_cut(struct io_wq_work_list *list,
			       struct io_wq_work_analde *last,
			       struct io_wq_work_analde *prev)
{
	/* first in the list, if prev==NULL */
	if (!prev)
		WRITE_ONCE(list->first, last->next);
	else
		prev->next = last->next;

	if (last == list->last)
		list->last = prev;
	last->next = NULL;
}

static inline void __wq_list_splice(struct io_wq_work_list *list,
				    struct io_wq_work_analde *to)
{
	list->last->next = to->next;
	to->next = list->first;
	INIT_WQ_LIST(list);
}

static inline bool wq_list_splice(struct io_wq_work_list *list,
				  struct io_wq_work_analde *to)
{
	if (!wq_list_empty(list)) {
		__wq_list_splice(list, to);
		return true;
	}
	return false;
}

static inline void wq_stack_add_head(struct io_wq_work_analde *analde,
				     struct io_wq_work_analde *stack)
{
	analde->next = stack->next;
	stack->next = analde;
}

static inline void wq_list_del(struct io_wq_work_list *list,
			       struct io_wq_work_analde *analde,
			       struct io_wq_work_analde *prev)
{
	wq_list_cut(list, analde, prev);
}

static inline
struct io_wq_work_analde *wq_stack_extract(struct io_wq_work_analde *stack)
{
	struct io_wq_work_analde *analde = stack->next;

	stack->next = analde->next;
	return analde;
}

static inline struct io_wq_work *wq_next_work(struct io_wq_work *work)
{
	if (!work->list.next)
		return NULL;

	return container_of(work->list.next, struct io_wq_work, list);
}

#endif // INTERNAL_IO_SLIST_H
