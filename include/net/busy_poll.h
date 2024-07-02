/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * net busy poll support
 * Copyright(c) 2013 Intel Corporation.
 *
 * Author: Eliezer Tamir
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 */

#ifndef _LINUX_NET_BUSY_POLL_H
#define _LINUX_NET_BUSY_POLL_H

#include <linux/netdevice.h>
#include <linux/sched/clock.h>
#include <linux/sched/signal.h>
#include <net/ip.h>
#include <net/xdp.h>

/*		0 - Reserved to indicate value not set
 *     1..NR_CPUS - Reserved for sender_cpu
 *  NR_CPUS+1..~0 - Region available for NAPI IDs
 */
#define MIN_NAPI_ID ((unsigned int)(NR_CPUS + 1))

#define BUSY_POLL_BUDGET 8

#ifdef CONFIG_NET_RX_BUSY_POLL

struct napi_struct;
extern unsigned int sysctl_net_busy_read __read_mostly;
extern unsigned int sysctl_net_busy_poll __read_mostly;

static inline bool net_busy_loop_on(void)
{
	return READ_ONCE(sysctl_net_busy_poll);
}

static inline bool sk_can_busy_loop(const struct sock *sk)
{
	return READ_ONCE(sk->sk_ll_usec) && !signal_pending(current);
}

bool sk_busy_loop_end(void *p, unsigned long start_time);

void napi_busy_loop(unsigned int napi_id,
		    bool (*loop_end)(void *, unsigned long),
		    void *loop_end_arg, bool prefer_busy_poll, u16 budget);

void napi_busy_loop_rcu(unsigned int napi_id,
			bool (*loop_end)(void *, unsigned long),
			void *loop_end_arg, bool prefer_busy_poll, u16 budget);

#else /* CONFIG_NET_RX_BUSY_POLL */
static inline unsigned long net_busy_loop_on(void)
{
	return 0;
}

static inline bool sk_can_busy_loop(struct sock *sk)
{
	return false;
}

#endif /* CONFIG_NET_RX_BUSY_POLL */

static inline unsigned long busy_loop_current_time(void)
{
#ifdef CONFIG_NET_RX_BUSY_POLL
	return (unsigned long)(local_clock() >> 10);
#else
	return 0;
#endif
}

/* in poll/select we use the global sysctl_net_ll_poll value */
static inline bool busy_loop_timeout(unsigned long start_time)
{
#ifdef CONFIG_NET_RX_BUSY_POLL
	unsigned long bp_usec = READ_ONCE(sysctl_net_busy_poll);

	if (bp_usec) {
		unsigned long end_time = start_time + bp_usec;
		unsigned long now = busy_loop_current_time();

		return time_after(now, end_time);
	}
#endif
	return true;
}

static inline bool sk_busy_loop_timeout(struct sock *sk,
					unsigned long start_time)
{
#ifdef CONFIG_NET_RX_BUSY_POLL
	unsigned long bp_usec = READ_ONCE(sk->sk_ll_usec);

	if (bp_usec) {
		unsigned long end_time = start_time + bp_usec;
		unsigned long now = busy_loop_current_time();

		return time_after(now, end_time);
	}
#endif
	return true;
}

static inline void sk_busy_loop(struct sock *sk, int nonblock)
{
#ifdef CONFIG_NET_RX_BUSY_POLL
	unsigned int napi_id = READ_ONCE(sk->sk_napi_id);

	if (napi_id >= MIN_NAPI_ID)
		napi_busy_loop(napi_id, nonblock ? NULL : sk_busy_loop_end, sk,
			       READ_ONCE(sk->sk_prefer_busy_poll),
			       READ_ONCE(sk->sk_busy_poll_budget) ?: BUSY_POLL_BUDGET);
#endif
}

/* used in the NIC receive handler to mark the skb */
static inline void skb_mark_napi_id(struct sk_buff *skb,
				    struct napi_struct *napi)
{
#ifdef CONFIG_NET_RX_BUSY_POLL
	/* If the skb was already marked with a valid NAPI ID, avoid overwriting
	 * it.
	 */
	if (skb->napi_id < MIN_NAPI_ID)
		skb->napi_id = napi->napi_id;
#endif
}

/* used in the protocol hanlder to propagate the napi_id to the socket */
static inline void sk_mark_napi_id(struct sock *sk, const struct sk_buff *skb)
{
#ifdef CONFIG_NET_RX_BUSY_POLL
	if (unlikely(READ_ONCE(sk->sk_napi_id) != skb->napi_id))
		WRITE_ONCE(sk->sk_napi_id, skb->napi_id);
#endif
	sk_rx_queue_update(sk, skb);
}

/* Variant of sk_mark_napi_id() for passive flow setup,
 * as sk->sk_napi_id and sk->sk_rx_queue_mapping content
 * needs to be set.
 */
static inline void sk_mark_napi_id_set(struct sock *sk,
				       const struct sk_buff *skb)
{
#ifdef CONFIG_NET_RX_BUSY_POLL
	WRITE_ONCE(sk->sk_napi_id, skb->napi_id);
#endif
	sk_rx_queue_set(sk, skb);
}

static inline void __sk_mark_napi_id_once(struct sock *sk, unsigned int napi_id)
{
#ifdef CONFIG_NET_RX_BUSY_POLL
	if (!READ_ONCE(sk->sk_napi_id))
		WRITE_ONCE(sk->sk_napi_id, napi_id);
#endif
}

/* variant used for unconnected sockets */
static inline void sk_mark_napi_id_once(struct sock *sk,
					const struct sk_buff *skb)
{
#ifdef CONFIG_NET_RX_BUSY_POLL
	__sk_mark_napi_id_once(sk, skb->napi_id);
#endif
}

static inline void sk_mark_napi_id_once_xdp(struct sock *sk,
					    const struct xdp_buff *xdp)
{
#ifdef CONFIG_NET_RX_BUSY_POLL
	__sk_mark_napi_id_once(sk, xdp->rxq->napi_id);
#endif
}

#endif /* _LINUX_NET_BUSY_POLL_H */
