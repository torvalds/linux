#ifndef _LISTHELP_H
#define _LISTHELP_H
#include <linux/config.h>
#include <linux/list.h>
#include <linux/netfilter_ipv4/lockhelp.h>

/* Header to do more comprehensive job than linux/list.h; assume list
   is first entry in structure. */

/* Return pointer to first true entry, if any, or NULL.  A macro
   required to allow inlining of cmpfn. */
#define LIST_FIND(head, cmpfn, type, args...)		\
({							\
	const struct list_head *__i, *__j = NULL;	\
							\
	ASSERT_READ_LOCK(head);				\
	list_for_each(__i, (head))			\
		if (cmpfn((const type)__i , ## args)) {	\
			__j = __i;			\
			break;				\
		}					\
	(type)__j;					\
})

#define LIST_FIND_W(head, cmpfn, type, args...)		\
({							\
	const struct list_head *__i, *__j = NULL;	\
							\
	ASSERT_WRITE_LOCK(head);			\
	list_for_each(__i, (head))			\
		if (cmpfn((type)__i , ## args)) {	\
			__j = __i;			\
			break;				\
		}					\
	(type)__j;					\
})

/* Just like LIST_FIND but we search backwards */
#define LIST_FIND_B(head, cmpfn, type, args...)		\
({							\
	const struct list_head *__i, *__j = NULL;	\
							\
	ASSERT_READ_LOCK(head);				\
	list_for_each_prev(__i, (head))			\
		if (cmpfn((const type)__i , ## args)) {	\
			__j = __i;			\
			break;				\
		}					\
	(type)__j;					\
})

static inline int
__list_cmp_same(const void *p1, const void *p2) { return p1 == p2; }

/* Is this entry in the list? */
static inline int
list_inlist(struct list_head *head, const void *entry)
{
	return LIST_FIND(head, __list_cmp_same, void *, entry) != NULL;
}

/* Delete from list. */
#ifdef CONFIG_NETFILTER_DEBUG
#define LIST_DELETE(head, oldentry)					\
do {									\
	ASSERT_WRITE_LOCK(head);					\
	if (!list_inlist(head, oldentry))				\
		printk("LIST_DELETE: %s:%u `%s'(%p) not in %s.\n",	\
		       __FILE__, __LINE__, #oldentry, oldentry, #head);	\
        else list_del((struct list_head *)oldentry);			\
} while(0)
#else
#define LIST_DELETE(head, oldentry) list_del((struct list_head *)oldentry)
#endif

/* Append. */
static inline void
list_append(struct list_head *head, void *new)
{
	ASSERT_WRITE_LOCK(head);
	list_add((new), (head)->prev);
}

/* Prepend. */
static inline void
list_prepend(struct list_head *head, void *new)
{
	ASSERT_WRITE_LOCK(head);
	list_add(new, head);
}

/* Insert according to ordering function; insert before first true. */
#define LIST_INSERT(head, new, cmpfn)				\
do {								\
	struct list_head *__i;					\
	ASSERT_WRITE_LOCK(head);				\
	list_for_each(__i, (head))				\
		if ((new), (typeof (new))__i)			\
			break;					\
	list_add((struct list_head *)(new), __i->prev);		\
} while(0)

/* If the field after the list_head is a nul-terminated string, you
   can use these functions. */
static inline int __list_cmp_name(const void *i, const char *name)
{
	return strcmp(name, i+sizeof(struct list_head)) == 0;
}

/* Returns false if same name already in list, otherwise does insert. */
static inline int
list_named_insert(struct list_head *head, void *new)
{
	if (LIST_FIND(head, __list_cmp_name, void *,
		      new + sizeof(struct list_head)))
		return 0;
	list_prepend(head, new);
	return 1;
}

/* Find this named element in the list. */
#define list_named_find(head, name)			\
LIST_FIND(head, __list_cmp_name, void *, name)

#endif /*_LISTHELP_H*/
