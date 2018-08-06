/*
 * net busy poll support
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

#ifndef _LINUX_NET_BUSY_POLL_H
#define _LINUX_NET_BUSY_POLL_H

#include <linux/netdevice.h>
#include <linux/sched/clock.h>
#include <linux/sched/signal.h>
#include <net/ip.h>

/*		0 - Reserved to indicate value not set
 *     1..NR_CPUS - Reserved for sender_cpu
 *  NR_CPUS+1..~0 - Region available for NAPI IDs
 */
#define MIN_NAPI_ID ((unsigned int)(NR_CPUS + 1))

#ifdef CONFIG_NET_RX_BUSY_POLL

struct napi_struct;
extern unsigned int sysctl_net_busy_read __read_mostly;
extern unsigned int sysctl_net_busy_poll __read_mostly;

static inline bool net_busy_loop_on(void)
{
	return sysctl_net_busy_poll;
}

static inline bool sk_can_busy_loop(const struct sock *sk)
{
	return sk->sk_ll_usec && !signal_pending(current);
}

bool sk_busy_loop_end(void *p, unsigned long start_time);

void napi_busy_loop(unsigned int napi_id,
		    bool (*loop_end)(void *, unsigned long),
		    void *loop_end_arg);

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
		napi_busy_loop(napi_id, nonblock ? NULL : sk_busy_loop_end, sk);
#endif
}

/* used in the NIC receive handler to mark the skb */
static inline void skb_mark_napi_id(struct sk_buff *skb,
				    struct napi_struct *napi)
{
#ifdef CONFIG_NET_RX_BUSY_POLL
	skb->napi_id = napi->napi_id;
#endif
}

/* used in the protocol hanlder to propagate the napi_id to the socket */
static inline void sk_mark_napi_id(struct sock *sk, const struct sk_buff *skb)
{
#ifdef CONFIG_NET_RX_BUSY_POLL
	sk->sk_napi_id = skb->napi_id;
#endif
	sk_rx_queue_set(sk, skb);
}

/* variant used for unconnected sockets */
static inline void sk_mark_napi_id_once(struct sock *sk,
					const struct sk_buff *skb)
{
#ifdef CONFIG_NET_RX_BUSY_POLL
	if (!sk->sk_napi_id)
		sk->sk_napi_id = skb->napi_id;
#endif
}

#endif /* _LINUX_NET_BUSY_POLL_H */
