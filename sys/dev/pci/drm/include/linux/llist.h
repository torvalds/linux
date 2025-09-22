/* Public domain. */

#ifndef _LINUX_LLIST_H
#define _LINUX_LLIST_H

#include <sys/atomic.h>

struct llist_node {
	struct llist_node *next;
};

struct llist_head {
	struct llist_node *first;
};

#define llist_entry(ptr, type, member)	container_of(ptr, type, member)

static inline struct llist_node *
llist_del_all(struct llist_head *head)
{
	return atomic_swap_ptr(&head->first, NULL);
}

static inline struct llist_node *
llist_del_first(struct llist_head *head)
{
	struct llist_node *first, *next;

	do {
		first = head->first;
		if (first == NULL)
			return NULL;
		next = first->next;
	} while (atomic_cas_ptr(&head->first, first, next) != first);

	return first;
}

static inline bool
llist_add(struct llist_node *new, struct llist_head *head)
{
	struct llist_node *first;

	do {
		new->next = first = head->first;
	} while (atomic_cas_ptr(&head->first, first, new) != first);

	return (first == NULL);
}

static inline bool
llist_add_batch(struct llist_node *new_first, struct llist_node *new_last,
    struct llist_head *head)
{
	struct llist_node *first;

	do {
		new_last->next = first = head->first;
	} while (atomic_cas_ptr(&head->first, first, new_first) != first);

	return (first == NULL);
}

static inline void
init_llist_head(struct llist_head *head)
{
	head->first = NULL;
}

static inline bool
llist_empty(struct llist_head *head)
{
	return (head->first == NULL);
}

#define llist_for_each_safe(pos, n, node)				\
	for ((pos) = (node);						\
	    (pos) != NULL &&						\
	    ((n) = (pos)->next, pos);					\
	    (pos) = (n))

#define llist_for_each_entry_safe(pos, n, node, member) 		\
	for (pos = llist_entry((node), __typeof(*pos), member); 	\
	    ((char *)(pos) + offsetof(typeof(*(pos)), member)) != NULL && \
	    (n = llist_entry(pos->member.next, __typeof(*pos), member), pos); \
	    pos = n)

#define llist_for_each_entry(pos, node, member)				\
	for ((pos) = llist_entry((node), __typeof(*(pos)), member);	\
	    ((char *)(pos) + offsetof(typeof(*(pos)), member)) != NULL;	\
	    (pos) = llist_entry((pos)->member.next, __typeof(*(pos)), member))

#endif
