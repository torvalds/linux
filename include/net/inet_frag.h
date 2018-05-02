/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_FRAG_H__
#define __NET_FRAG_H__

#include <linux/rhashtable.h>

struct netns_frags {
	/* sysctls */
	long			high_thresh;
	long			low_thresh;
	int			timeout;
	int			max_dist;
	struct inet_frags	*f;

	struct rhashtable       rhashtable ____cacheline_aligned_in_smp;

	/* Keep atomic mem on separate cachelines in structs that include it */
	atomic_long_t		mem ____cacheline_aligned_in_smp;
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

struct frag_v4_compare_key {
	__be32		saddr;
	__be32		daddr;
	u32		user;
	u32		vif;
	__be16		id;
	u16		protocol;
};

struct frag_v6_compare_key {
	struct in6_addr	saddr;
	struct in6_addr	daddr;
	u32		user;
	__be32		id;
	u32		iif;
};

/**
 * struct inet_frag_queue - fragment queue
 *
 * @node: rhash node
 * @key: keys identifying this frag.
 * @timer: queue expiration timer
 * @lock: spinlock protecting this frag
 * @refcnt: reference count of the queue
 * @fragments: received fragments head
 * @fragments_tail: received fragments tail
 * @stamp: timestamp of the last received fragment
 * @len: total length of the original datagram
 * @meat: length of received fragments so far
 * @flags: fragment queue flags
 * @max_size: maximum received fragment size
 * @net: namespace that this frag belongs to
 * @rcu: rcu head for freeing deferall
 */
struct inet_frag_queue {
	struct rhash_head	node;
	union {
		struct frag_v4_compare_key v4;
		struct frag_v6_compare_key v6;
	} key;
	struct timer_list	timer;
	spinlock_t		lock;
	refcount_t		refcnt;
	struct sk_buff		*fragments;
	struct sk_buff		*fragments_tail;
	ktime_t			stamp;
	int			len;
	int			meat;
	__u8			flags;
	u16			max_size;
	struct netns_frags      *net;
	struct rcu_head		rcu;
};

struct inet_frags {
	unsigned int		qsize;

	void			(*constructor)(struct inet_frag_queue *q,
					       const void *arg);
	void			(*destructor)(struct inet_frag_queue *);
	void			(*frag_expire)(struct timer_list *t);
	struct kmem_cache	*frags_cachep;
	const char		*frags_cache_name;
	struct rhashtable_params rhash_params;
};

int inet_frags_init(struct inet_frags *);
void inet_frags_fini(struct inet_frags *);

static inline int inet_frags_init_net(struct netns_frags *nf)
{
	atomic_long_set(&nf->mem, 0);
	return rhashtable_init(&nf->rhashtable, &nf->f->rhash_params);
}
void inet_frags_exit_net(struct netns_frags *nf);

void inet_frag_kill(struct inet_frag_queue *q);
void inet_frag_destroy(struct inet_frag_queue *q);
struct inet_frag_queue *inet_frag_find(struct netns_frags *nf, void *key);

static inline void inet_frag_put(struct inet_frag_queue *q)
{
	if (refcount_dec_and_test(&q->refcnt))
		inet_frag_destroy(q);
}

/* Memory Tracking Functions. */

static inline long frag_mem_limit(const struct netns_frags *nf)
{
	return atomic_long_read(&nf->mem);
}

static inline void sub_frag_mem_limit(struct netns_frags *nf, long val)
{
	atomic_long_sub(val, &nf->mem);
}

static inline void add_frag_mem_limit(struct netns_frags *nf, long val)
{
	atomic_long_add(val, &nf->mem);
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
