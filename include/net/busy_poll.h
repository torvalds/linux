/*
 * Low Latency Sockets
 * Copyright(c) 2013 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Author: Eliezer Tamir
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 */

#ifndef _LINUX_NET_LL_POLL_H
#define _LINUX_NET_LL_POLL_H

#include <linux/netdevice.h>
#include <net/ip.h>

#ifdef CONFIG_NET_LL_RX_POLL

struct napi_struct;
extern unsigned int sysctl_net_ll_read __read_mostly;
extern unsigned int sysctl_net_ll_poll __read_mostly;

/* return values from ndo_ll_poll */
#define LL_FLUSH_FAILED		-1
#define LL_FLUSH_BUSY		-2

static inline bool net_busy_loop_on(void)
{
	return sysctl_net_ll_poll;
}

/* a wrapper to make debug_smp_processor_id() happy
 * we can use sched_clock() because we don't care much about precision
 * we only care that the average is bounded
 */
#ifdef CONFIG_DEBUG_PREEMPT
static inline u64 busy_loop_us_clock(void)
{
	u64 rc;

	preempt_disable_notrace();
	rc = sched_clock();
	preempt_enable_no_resched_notrace();

	return rc >> 10;
}
#else /* CONFIG_DEBUG_PREEMPT */
static inline u64 busy_loop_us_clock(void)
{
	return sched_clock() >> 10;
}
#endif /* CONFIG_DEBUG_PREEMPT */

static inline unsigned long sk_busy_loop_end_time(struct sock *sk)
{
	return busy_loop_us_clock() + ACCESS_ONCE(sk->sk_ll_usec);
}

/* in poll/select we use the global sysctl_net_ll_poll value */
static inline unsigned long busy_loop_end_time(void)
{
	return busy_loop_us_clock() + ACCESS_ONCE(sysctl_net_ll_poll);
}

static inline bool sk_can_busy_loop(struct sock *sk)
{
	return sk->sk_ll_usec && sk->sk_napi_id &&
	       !need_resched() && !signal_pending(current);
}


static inline bool busy_loop_timeout(unsigned long end_time)
{
	unsigned long now = busy_loop_us_clock();

	return time_after(now, end_time);
}

/* when used in sock_poll() nonblock is known at compile time to be true
 * so the loop and end_time will be optimized out
 */
static inline bool sk_busy_loop(struct sock *sk, int nonblock)
{
	unsigned long end_time = !nonblock ? sk_busy_loop_end_time(sk) : 0;
	const struct net_device_ops *ops;
	struct napi_struct *napi;
	int rc = false;

	/*
	 * rcu read lock for napi hash
	 * bh so we don't race with net_rx_action
	 */
	rcu_read_lock_bh();

	napi = napi_by_id(sk->sk_napi_id);
	if (!napi)
		goto out;

	ops = napi->dev->netdev_ops;
	if (!ops->ndo_ll_poll)
		goto out;

	do {
		rc = ops->ndo_ll_poll(napi);

		if (rc == LL_FLUSH_FAILED)
			break; /* permanent failure */

		if (rc > 0)
			/* local bh are disabled so it is ok to use _BH */
			NET_ADD_STATS_BH(sock_net(sk),
					 LINUX_MIB_LOWLATENCYRXPACKETS, rc);

	} while (!nonblock && skb_queue_empty(&sk->sk_receive_queue) &&
		 !need_resched() && !busy_loop_timeout(end_time));

	rc = !skb_queue_empty(&sk->sk_receive_queue);
out:
	rcu_read_unlock_bh();
	return rc;
}

/* used in the NIC receive handler to mark the skb */
static inline void skb_mark_ll(struct sk_buff *skb, struct napi_struct *napi)
{
	skb->napi_id = napi->napi_id;
}

/* used in the protocol hanlder to propagate the napi_id to the socket */
static inline void sk_mark_ll(struct sock *sk, struct sk_buff *skb)
{
	sk->sk_napi_id = skb->napi_id;
}

#else /* CONFIG_NET_LL_RX_POLL */
static inline unsigned long net_busy_loop_on(void)
{
	return 0;
}

static inline unsigned long busy_loop_end_time(void)
{
	return 0;
}

static inline bool sk_can_busy_loop(struct sock *sk)
{
	return false;
}

static inline bool sk_busy_poll(struct sock *sk, int nonblock)
{
	return false;
}

static inline void skb_mark_ll(struct sk_buff *skb, struct napi_struct *napi)
{
}

static inline void sk_mark_ll(struct sock *sk, struct sk_buff *skb)
{
}

static inline bool busy_loop_timeout(unsigned long end_time)
{
	return true;
}

#endif /* CONFIG_NET_LL_RX_POLL */
#endif /* _LINUX_NET_LL_POLL_H */
