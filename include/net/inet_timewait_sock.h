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


#include <linux/kmemcheck.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include <net/inet_sock.h>
#include <net/sock.h>
#include <net/tcp_states.h>
#include <net/timewait_sock.h>

#include <linux/atomic.h>

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
	unsigned long		twcal_jiffie;
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
extern void inet_twdr_twkill_work(struct work_struct *work);
extern void inet_twdr_twcal_tick(unsigned long data);

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
#define tw_node			__tw_common.skc_nulls_node
#define tw_bind_node		__tw_common.skc_bind_node
#define tw_refcnt		__tw_common.skc_refcnt
#define tw_hash			__tw_common.skc_hash
#define tw_prot			__tw_common.skc_prot
#define tw_net			__tw_common.skc_net
#define tw_daddr        	__tw_common.skc_daddr
#define tw_rcv_saddr    	__tw_common.skc_rcv_saddr
#define tw_addrpair		__tw_common.skc_addrpair
#define tw_dport		__tw_common.skc_dport
#define tw_num			__tw_common.skc_num
#define tw_portpair		__tw_common.skc_portpair

	int			tw_timeout;
	volatile unsigned char	tw_substate;
	unsigned char		tw_rcv_wscale;

	/* Socket demultiplex comparisons on incoming packets. */
	/* these three are in inet_sock */
	__be16			tw_sport;
	kmemcheck_bitfield_begin(flags);
	/* And these are ours. */
	unsigned int		tw_ipv6only     : 1,
				tw_transparent  : 1,
				tw_pad		: 6,	/* 6 bits hole */
				tw_tos		: 8,
				tw_ipv6_offset  : 16;
	kmemcheck_bitfield_end(flags);
	unsigned long		tw_ttd;
	struct inet_bind_bucket	*tw_tb;
	struct hlist_node	tw_death_node;
};
#define tw_tclass tw_tos

static inline void inet_twsk_add_node_rcu(struct inet_timewait_sock *tw,
				      struct hlist_nulls_head *list)
{
	hlist_nulls_add_head_rcu(&tw->tw_node, list);
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
	hlist_nulls_for_each_entry(tw, node, head, tw_node)

#define inet_twsk_for_each_inmate(tw, jail) \
	hlist_for_each_entry(tw, jail, tw_death_node)

#define inet_twsk_for_each_inmate_safe(tw, safe, jail) \
	hlist_for_each_entry_safe(tw, safe, jail, tw_death_node)

static inline struct inet_timewait_sock *inet_twsk(const struct sock *sk)
{
	return (struct inet_timewait_sock *)sk;
}

static inline __be32 sk_rcv_saddr(const struct sock *sk)
{
/* both inet_sk() and inet_twsk() store rcv_saddr in skc_rcv_saddr */
	return sk->__sk_common.skc_rcv_saddr;
}

extern void inet_twsk_put(struct inet_timewait_sock *tw);

extern int inet_twsk_unhash(struct inet_timewait_sock *tw);

extern int inet_twsk_bind_unhash(struct inet_timewait_sock *tw,
				 struct inet_hashinfo *hashinfo);

extern struct inet_timewait_sock *inet_twsk_alloc(const struct sock *sk,
						  const int state);

extern void __inet_twsk_hashdance(struct inet_timewait_sock *tw,
				  struct sock *sk,
				  struct inet_hashinfo *hashinfo);

extern void inet_twsk_schedule(struct inet_timewait_sock *tw,
			       struct inet_timewait_death_row *twdr,
			       const int timeo, const int timewait_len);
extern void inet_twsk_deschedule(struct inet_timewait_sock *tw,
				 struct inet_timewait_death_row *twdr);

extern void inet_twsk_purge(struct inet_hashinfo *hashinfo,
			    struct inet_timewait_death_row *twdr, int family);

static inline
struct net *twsk_net(const struct inet_timewait_sock *twsk)
{
	return read_pnet(&twsk->tw_net);
}

static inline
void twsk_net_set(struct inet_timewait_sock *twsk, struct net *net)
{
	write_pnet(&twsk->tw_net, net);
}
#endif	/* _INET_TIMEWAIT_SOCK_ */
