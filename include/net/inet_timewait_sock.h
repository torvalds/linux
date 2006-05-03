/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for a generic INET TIMEWAIT sock
 *
 *		From code originally in net/tcp.h
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _INET_TIMEWAIT_SOCK_
#define _INET_TIMEWAIT_SOCK_

#include <linux/config.h>

#include <linux/list.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include <net/inet_sock.h>
#include <net/sock.h>
#include <net/tcp_states.h>
#include <net/timewait_sock.h>

#include <asm/atomic.h>

struct inet_hashinfo;

#define INET_TWDR_RECYCLE_SLOTS_LOG	5
#define INET_TWDR_RECYCLE_SLOTS		(1 << INET_TWDR_RECYCLE_SLOTS_LOG)

/*
 * If time > 4sec, it is "slow" path, no recycling is required,
 * so that we select tick to get range about 4 seconds.
 */
#if HZ <= 16 || HZ > 4096
# error Unsupported: HZ <= 16 or HZ > 4096
#elif HZ <= 32
# define INET_TWDR_RECYCLE_TICK (5 + 2 - INET_TWDR_RECYCLE_SLOTS_LOG)
#elif HZ <= 64
# define INET_TWDR_RECYCLE_TICK (6 + 2 - INET_TWDR_RECYCLE_SLOTS_LOG)
#elif HZ <= 128
# define INET_TWDR_RECYCLE_TICK (7 + 2 - INET_TWDR_RECYCLE_SLOTS_LOG)
#elif HZ <= 256
# define INET_TWDR_RECYCLE_TICK (8 + 2 - INET_TWDR_RECYCLE_SLOTS_LOG)
#elif HZ <= 512
# define INET_TWDR_RECYCLE_TICK (9 + 2 - INET_TWDR_RECYCLE_SLOTS_LOG)
#elif HZ <= 1024
# define INET_TWDR_RECYCLE_TICK (10 + 2 - INET_TWDR_RECYCLE_SLOTS_LOG)
#elif HZ <= 2048
# define INET_TWDR_RECYCLE_TICK (11 + 2 - INET_TWDR_RECYCLE_SLOTS_LOG)
#else
# define INET_TWDR_RECYCLE_TICK (12 + 2 - INET_TWDR_RECYCLE_SLOTS_LOG)
#endif

/* TIME_WAIT reaping mechanism. */
#define INET_TWDR_TWKILL_SLOTS	8 /* Please keep this a power of 2. */

#define INET_TWDR_TWKILL_QUOTA 100

struct inet_timewait_death_row {
	/* Short-time timewait calendar */
	int			twcal_hand;
	int			twcal_jiffie;
	struct timer_list	twcal_timer;
	struct hlist_head	twcal_row[INET_TWDR_RECYCLE_SLOTS];

	spinlock_t		death_lock;
	int			tw_count;
	int			period;
	u32			thread_slots;
	struct work_struct	twkill_work;
	struct timer_list	tw_timer;
	int			slot;
	struct hlist_head	cells[INET_TWDR_TWKILL_SLOTS];
	struct inet_hashinfo 	*hashinfo;
	int			sysctl_tw_recycle;
	int			sysctl_max_tw_buckets;
};

extern void inet_twdr_hangman(unsigned long data);
extern void inet_twdr_twkill_work(void *data);
extern void inet_twdr_twcal_tick(unsigned long data);

#if (BITS_PER_LONG == 64)
#define INET_TIMEWAIT_ADDRCMP_ALIGN_BYTES 8
#else
#define INET_TIMEWAIT_ADDRCMP_ALIGN_BYTES 4
#endif

struct inet_bind_bucket;

/*
 * This is a TIME_WAIT sock. It works around the memory consumption
 * problems of sockets in such a state on heavily loaded servers, but
 * without violating the protocol specification.
 */
struct inet_timewait_sock {
	/*
	 * Now struct sock also uses sock_common, so please just
	 * don't add nothing before this first member (__tw_common) --acme
	 */
	struct sock_common	__tw_common;
#define tw_family		__tw_common.skc_family
#define tw_state		__tw_common.skc_state
#define tw_reuse		__tw_common.skc_reuse
#define tw_bound_dev_if		__tw_common.skc_bound_dev_if
#define tw_node			__tw_common.skc_node
#define tw_bind_node		__tw_common.skc_bind_node
#define tw_refcnt		__tw_common.skc_refcnt
#define tw_hash			__tw_common.skc_hash
#define tw_prot			__tw_common.skc_prot
	volatile unsigned char	tw_substate;
	/* 3 bits hole, try to pack */
	unsigned char		tw_rcv_wscale;
	/* Socket demultiplex comparisons on incoming packets. */
	/* these five are in inet_sock */
	__u16			tw_sport;
	__u32			tw_daddr __attribute__((aligned(INET_TIMEWAIT_ADDRCMP_ALIGN_BYTES)));
	__u32			tw_rcv_saddr;
	__u16			tw_dport;
	__u16			tw_num;
	/* And these are ours. */
	__u8			tw_ipv6only:1;
	/* 15 bits hole, try to pack */
	__u16			tw_ipv6_offset;
	int			tw_timeout;
	unsigned long		tw_ttd;
	struct inet_bind_bucket	*tw_tb;
	struct hlist_node	tw_death_node;
};

static inline void inet_twsk_add_node(struct inet_timewait_sock *tw,
				      struct hlist_head *list)
{
	hlist_add_head(&tw->tw_node, list);
}

static inline void inet_twsk_add_bind_node(struct inet_timewait_sock *tw,
					   struct hlist_head *list)
{
	hlist_add_head(&tw->tw_bind_node, list);
}

static inline int inet_twsk_dead_hashed(const struct inet_timewait_sock *tw)
{
	return !hlist_unhashed(&tw->tw_death_node);
}

static inline void inet_twsk_dead_node_init(struct inet_timewait_sock *tw)
{
	tw->tw_death_node.pprev = NULL;
}

static inline void __inet_twsk_del_dead_node(struct inet_timewait_sock *tw)
{
	__hlist_del(&tw->tw_death_node);
	inet_twsk_dead_node_init(tw);
}

static inline int inet_twsk_del_dead_node(struct inet_timewait_sock *tw)
{
	if (inet_twsk_dead_hashed(tw)) {
		__inet_twsk_del_dead_node(tw);
		return 1;
	}
	return 0;
}

#define inet_twsk_for_each(tw, node, head) \
	hlist_for_each_entry(tw, node, head, tw_node)

#define inet_twsk_for_each_inmate(tw, node, jail) \
	hlist_for_each_entry(tw, node, jail, tw_death_node)

#define inet_twsk_for_each_inmate_safe(tw, node, safe, jail) \
	hlist_for_each_entry_safe(tw, node, safe, jail, tw_death_node)

static inline struct inet_timewait_sock *inet_twsk(const struct sock *sk)
{
	return (struct inet_timewait_sock *)sk;
}

static inline u32 inet_rcv_saddr(const struct sock *sk)
{
	return likely(sk->sk_state != TCP_TIME_WAIT) ?
		inet_sk(sk)->rcv_saddr : inet_twsk(sk)->tw_rcv_saddr;
}

static inline void inet_twsk_put(struct inet_timewait_sock *tw)
{
	if (atomic_dec_and_test(&tw->tw_refcnt)) {
		struct module *owner = tw->tw_prot->owner;
#ifdef SOCK_REFCNT_DEBUG
		printk(KERN_DEBUG "%s timewait_sock %p released\n",
		       tw->tw_prot->name, tw);
#endif
		kmem_cache_free(tw->tw_prot->twsk_prot->twsk_slab, tw);
		module_put(owner);
	}
}

extern struct inet_timewait_sock *inet_twsk_alloc(const struct sock *sk,
						  const int state);

extern void __inet_twsk_kill(struct inet_timewait_sock *tw,
			     struct inet_hashinfo *hashinfo);

extern void __inet_twsk_hashdance(struct inet_timewait_sock *tw,
				  struct sock *sk,
				  struct inet_hashinfo *hashinfo);

extern void inet_twsk_schedule(struct inet_timewait_sock *tw,
			       struct inet_timewait_death_row *twdr,
			       const int timeo, const int timewait_len);
extern void inet_twsk_deschedule(struct inet_timewait_sock *tw,
				 struct inet_timewait_death_row *twdr);
#endif	/* _INET_TIMEWAIT_SOCK_ */
