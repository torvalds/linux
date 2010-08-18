#ifndef __NET_FRAG_H__
#define __NET_FRAG_H__

struct netns_frags {
	int			nqueues;
	atomic_t		mem;
	struct list_head	lru_list;

	/* sysctls */
	int			timeout;
	int			high_thresh;
	int			low_thresh;
};

struct inet_frag_queue {
	struct hlist_node	list;
	struct netns_frags	*net;
	struct list_head	lru_list;   /* lru list member */
	spinlock_t		lock;
	atomic_t		refcnt;
	struct timer_list	timer;      /* when will this queue expire? */
	struct sk_buff		*fragments; /* list of received fragments */
	struct sk_buff		*fragments_tail;
	ktime_t			stamp;
	int			len;        /* total length of orig datagram */
	int			meat;
	__u8			last_in;    /* first/last segment arrived? */

#define INET_FRAG_COMPLETE	4
#define INET_FRAG_FIRST_IN	2
#define INET_FRAG_LAST_IN	1
};

#define INETFRAGS_HASHSZ		64

struct inet_frags {
	struct hlist_head	hash[INETFRAGS_HASHSZ];
	rwlock_t		lock;
	u32			rnd;
	int			qsize;
	int			secret_interval;
	struct timer_list	secret_timer;

	unsigned int		(*hashfn)(struct inet_frag_queue *);
	void			(*constructor)(struct inet_frag_queue *q,
						void *arg);
	void			(*destructor)(struct inet_frag_queue *);
	void			(*skb_free)(struct sk_buff *);
	int			(*match)(struct inet_frag_queue *q,
						void *arg);
	void			(*frag_expire)(unsigned long data);
};

void inet_frags_init(struct inet_frags *);
void inet_frags_fini(struct inet_frags *);

void inet_frags_init_net(struct netns_frags *nf);
void inet_frags_exit_net(struct netns_frags *nf, struct inet_frags *f);

void inet_frag_kill(struct inet_frag_queue *q, struct inet_frags *f);
void inet_frag_destroy(struct inet_frag_queue *q,
				struct inet_frags *f, int *work);
int inet_frag_evictor(struct netns_frags *nf, struct inet_frags *f);
struct inet_frag_queue *inet_frag_find(struct netns_frags *nf,
		struct inet_frags *f, void *key, unsigned int hash)
	__releases(&f->lock);

static inline void inet_frag_put(struct inet_frag_queue *q, struct inet_frags *f)
{
	if (atomic_dec_and_test(&q->refcnt))
		inet_frag_destroy(q, f, NULL);
}

#endif
