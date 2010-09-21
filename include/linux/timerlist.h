#ifndef _LINUX_TIMERLIST_H
#define _LINUX_TIMERLIST_H

#include <linux/rbtree.h>
#include <linux/ktime.h>


struct timerlist_node {
	struct rb_node node;
	ktime_t expires;
};

struct timerlist_head {
	struct rb_root head;
	struct timerlist_node *next;
};


extern void timerlist_add(struct timerlist_head *head,
				struct timerlist_node *node);
extern void timerlist_del(struct timerlist_head *head,
				struct timerlist_node *node);
extern struct timerlist_node *timerlist_getnext(struct timerlist_head *head);
extern struct timerlist_node *timerlist_iterate_next(
						struct timerlist_node *node);

static inline void timerlist_init(struct timerlist_node *node)
{
	RB_CLEAR_NODE(&node->node);
}

static inline void timerlist_init_head(struct timerlist_head *head)
{
	head->head = RB_ROOT;
	head->next = NULL;
}
#endif /* _LINUX_TIMERLIST_H */
