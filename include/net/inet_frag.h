#ifndef __NET_FRAG_H__
#define __NET_FRAG_H__

#include <linux/percpu_counter.h>

struct netns_frags {
	/* The percpu_counter "mem" need to be cacheline aligned.
	 *  mem.count must not share cacheline with other writers
	 */
	struct percpu_counter   mem ____cacheline_aligned_in_smp;

	/* sysctls */
	int			timeout;
	int			high_thresh;
	int			low_thresh;
};

/**
 * fragment queue flags
 *
 * @INET_FRAG_FIRST_IN: first fragment has arrived
 * @INET_FRAG_LAST_IN: final fragment has arrived
 * @INET_FRAG_COMPLETE: frag queue has been processed and is due for destruction
 */
enum {
	INET_FRAG_FIRST_IN	= BIT(0),
	INET_FRAG_LAST_IN	= BIT(1),
	INET_FRAG_COMPLETE	= BIT(2),
};

/**
 * struct inet_frag_queue - fragment queue
 *
 * @lock: spinlock protecting the queue
 * @timer: queue expiration timer
 * @list: hash bucket list
 * @refcnt: reference count of the queue
 * @fragments: received fragments head
 * @fragments_tail: received fragments tail
 * @stamp: timestamp of the last received fragment
 * @len: total length of the original datagram
 * @meat: length of received fragments so far
 * @flags: fragment queue flags
 * @max_size: maximum received fragment size
 * @net: namespace that this frag belongs to
 * @list_evictor: list of queues to forcefully evict (e.g. due to low memory)
 */
struct inet_frag_queue {
	spinlock_t		lock;
	struct timer_list	timer;
	struct hlist_node	list;
	atomic_t		refcnt;
	struct sk_buff		*fragments;
	struct sk_buff		*fragments_tail;
	ktime_t			stamp;
	int			len;
	int			meat;
	__u8			flags;
	u16			max_size;
	struct netns_frags	*net;
	struct hlist_node	list_evictor;
};

#define INETFRAGS_HASHSZ	1024

/* averaged:
 * max_depth = default ipfrag_high_thresh / INETFRAGS_HASHSZ /
 *	       rounded up (SKB_TRUELEN(0) + sizeof(struct ipq or
 *	       struct frag_queue))
 */
#define INETFRAGS_MAXDEPTH	128

struct inet_frag_bucket {
	struct hlist_head	chain;
	spinlock_t		chain_lock;
};

struct inet_frags {
	struct inet_frag_bucket	hash[INETFRAGS_HASHSZ];

	struct work_struct	frags_work;
	unsigned int next_bucket;
	unsigned long last_rebuild_jiffies;
	bool rebuild;

	/* The first call to hashfn is responsible to initialize
	 * rnd. This is best done with net_get_random_once.
	 *
	 * rnd_seqlock is used to let hash insertion detect
	 * when it needs to re-lookup the hash chain to use.
	 */
	u32			rnd;
	seqlock_t		rnd_seqlock;
	int			qsize;

	unsigned int		(*hashfn)(const struct inet_frag_queue *);
	bool			(*match)(const struct inet_frag_queue *q,
					 const void *arg);
	void			(*constructor)(struct inet_frag_queue *q,
					       const void *arg);
	void			(*destructor)(struct inet_frag_queue *);
	void			(*frag_expire)(unsigned long data);
	struct kmem_cache	*frags_cachep;
	const char		*frags_cache_name;
};

int inet_frags_init(struct inet_frags *);
void inet_frags_fini(struct inet_frags *);

static inline int inet_frags_init_net(struct netns_frags *nf)
{
	return percpu_counter_init(&nf->mem, 0, GFP_KERNEL);
}
static inline void inet_frags_uninit_net(struct netns_frags *nf)
{
	percpu_counter_destroy(&nf->mem);
}

void inet_frags_exit_net(struct netns_frags *nf, struct inet_frags *f);

void inet_frag_kill(struct inet_frag_queue *q, struct inet_frags *f);
void inet_frag_destroy(struct inet_frag_queue *q, struct inet_frags *f);
struct inet_frag_queue *inet_frag_find(struct netns_frags *nf,
		struct inet_frags *f, void *key, unsigned int hash);

void inet_frag_maybe_warn_overflow(struct inet_frag_queue *q,
				   const char *prefix);

static inline void inet_frag_put(struct inet_frag_queue *q, struct inet_frags *f)
{
	if (atomic_dec_and_test(&q->refcnt))
		inet_frag_destroy(q, f);
}

static inline bool inet_frag_evicting(struct inet_frag_queue *q)
{
	return !hlist_unhashed(&q->list_evictor);
}

/* Memory Tracking Functions. */

/* The default percpu_counter batch size is not big enough to scale to
 * fragmentation mem acct sizes.
 * The mem size of a 64K fragment is approx:
 *  (44 fragments * 2944 truesize) + frag_queue struct(200) = 129736 bytes
 */
static unsigned int frag_percpu_counter_batch = 130000;

static inline int frag_mem_limit(struct netns_frags *nf)
{
	return percpu_counter_read(&nf->mem);
}

static inline void sub_frag_mem_limit(struct netns_frags *nf, int i)
{
	__percpu_counter_add(&nf->mem, -i, frag_percpu_counter_batch);
}

static inline void add_frag_mem_limit(struct netns_frags *nf, int i)
{
	__percpu_counter_add(&nf->mem, i, frag_percpu_counter_batch);
}

static inline unsigned int sum_frag_mem_limit(struct netns_frags *nf)
{
	unsigned int res;

	local_bh_disable();
	res = percpu_counter_sum_positive(&nf->mem);
	local_bh_enable();

	return res;
}

/* RFC 3168 support :
 * We want to check ECN values of all fragments, do detect invalid combinations.
 * In ipq->ecn, we store the OR value of each ip4_frag_ecn() fragment value.
 */
#define	IPFRAG_ECN_NOT_ECT	0x01 /* one frag had ECN_NOT_ECT */
#define	IPFRAG_ECN_ECT_1	0x02 /* one frag had ECN_ECT_1 */
#define	IPFRAG_ECN_ECT_0	0x04 /* one frag had ECN_ECT_0 */
#define	IPFRAG_ECN_CE		0x08 /* one frag had ECN_CE */

extern const u8 ip_frag_ecn_table[16];

#endif
