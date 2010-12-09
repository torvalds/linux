#ifndef _LINUX_TIMERQUEUE_H
#define _LINUX_TIMERQUEUE_H

#include <linux/rbtree.h>
#include <linux/ktime.h>


struct timerqueue_node {
	struct rb_node node;
	ktime_t expires;
};

struct timerqueue_head {
	struct rb_root head;
	struct timerqueue_node *next;
};


extern void timerqueue_add(struct timerqueue_head *head,
				struct timerqueue_node *node);
extern void timerqueue_del(struct timerqueue_head *head,
				struct timerqueue_node *node);
extern struct timerqueue_node *timerqueue_getnext(struct timerqueue_head *head);
extern struct timerqueue_node *timerqueue_iterate_next(
						struct timerqueue_node *node);

static inline void timerqueue_init(struct timerqueue_node *node)
{
	RB_CLEAR_NODE(&node->node);
}

static inline void timerqueue_init_head(struct timerqueue_head *head)
{
	head->head = RB_ROOT;
	head->next = NULL;
}
#endif /* _LINUX_TIMERQUEUE_H */
