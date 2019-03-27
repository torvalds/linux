/*
 * $FreeBSD$
 *
 * linux-like bidirectional lists
 */

#ifndef _MYLIST_H
#define _MYLIST_H
/* not just a head, also the link field for a list entry */
struct list_head {
        struct list_head *prev, *next;
};

#define INIT_LIST_HEAD(l) do {  (l)->prev = (l)->next = (l); } while (0)
#define list_empty(l)   ( (l)->next == l )
static inline void
__list_add(struct list_head *o, struct list_head *prev,
        struct list_head *next)
{
        next->prev = o;
        o->next = next;
        o->prev = prev;
        prev->next = o;
}
 
static inline void
list_add_tail(struct list_head *o, struct list_head *head)
{
        __list_add(o, head->prev, head);
}

#define list_first_entry(pL, ty, member)        \
        (ty *)((char *)((pL)->next) - offsetof(ty, member))

static inline void
__list_del(struct list_head *prev, struct list_head *next)
{
        next->prev = prev;
        prev->next = next;
}

static inline void
list_del(struct list_head *entry)
{
	ND("called on %p", entry);
        __list_del(entry->prev, entry->next);
        entry->next = entry->prev = NULL;
}

#endif /* _MYLIST_H */
