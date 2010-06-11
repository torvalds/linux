#ifndef _LINUX_XLIST_H
#define _LINUX_XLIST_H

#include <linux/stddef.h>
#include <linux/poison.h>
#include <linux/prefetch.h>
#include <asm/system.h>

struct xlist_head {
	struct xlist_head *next;
};

/*
 * XLIST_PTR_TAIL can be used to prevent double insertion.  See
 * xlist_protect()
 */
#define XLIST_PTR_TAIL ((struct xlist_head *)0x1)

static inline void xlist_add(struct xlist_head *new, struct xlist_head *tail, struct xlist_head *head)
{
	struct xlist_head *cur;
	struct xlist_head *check;

	while (1) {
		cur = head->next;
		tail->next = cur;
		check = cmpxchg(&head->next, cur, new);
		if (check == cur)
			break;
	}
}

/*
 * To avoid duplicate insertion by two CPUs of the same xlist item
 * you can call xlist_protect.  It will stuff XLIST_PTR_TAIL
 * into the entry->next pointer with xchg, and only return 1
 * if there was a NULL there before.
 *
 * if xlist_protect returns zero, someone else is busy working
 * on this entry.  Getting a NULL into the entry in a race
 * free manner is the caller's job.
 */
static inline int xlist_protect(struct xlist_head *entry)
{
	struct xlist_head *val;

	val = xchg(&entry->next, XLIST_PTR_TAIL);
	if (val == NULL)
		return 1;
	return 0;
}

static inline struct xlist_head *xlist_del_head(struct xlist_head *head)
{
	struct xlist_head *cur;
	struct xlist_head *check;
	struct xlist_head *next;

	while (1) {
		cur = head->next;
		if (!cur)
			goto out;

		if (cur == XLIST_PTR_TAIL) {
			cur = NULL;
			goto out;
		}

		next = cur->next;
		check = cmpxchg(&head->next, cur, next);
		if (check == cur)
			goto out;
	}
out:
	return cur;
}

static inline struct xlist_head *xlist_del_head_fast(struct xlist_head *head)
{
	struct xlist_head *cur;

	cur = head->next;
	if (!cur || cur == XLIST_PTR_TAIL)
		return NULL;

	head->next = cur->next;
	return cur;
}

static inline void xlist_splice(struct xlist_head *list,
				struct xlist_head *head)
{
	struct xlist_head *cur;

	WARN_ON(head->next);
	cur = xchg(&list->next, NULL);
	head->next = cur;
}

static inline void INIT_XLIST_HEAD(struct xlist_head *list)
{
	list->next = NULL;
}

static inline int xlist_empty(struct xlist_head *head)
{
	return head->next == NULL || head->next == XLIST_PTR_TAIL;
}

#endif
