#ifndef __NET_FRAG_H__
#define __NET_FRAG_H__

struct inet_frag_queue {
	struct hlist_node	list;
	struct list_head	lru_list;   /* lru list member */
	spinlock_t		lock;
	atomic_t		refcnt;
	struct timer_list	timer;      /* when will this queue expire? */
	struct sk_buff		*fragments; /* list of received fragments */
	ktime_t			stamp;
	int			len;        /* total length of orig datagram */
	int			meat;
	__u8			last_in;    /* first/last segment arrived? */

#define COMPLETE		4
#define FIRST_IN		2
#define LAST_IN			1
};

#endif
