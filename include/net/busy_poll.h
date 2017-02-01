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
#include <net/ip.h>

#ifdef CONFIG_NET_RX_BUSY_POLL

struct napi_struct;
extern unsigned int sysctl_net_busy_read __read_mostly;
extern unsigned int sysctl_net_busy_poll __read_mostly;

static inline bool net_busy_loop_on(void)
{
	return sysctl_net_busy_poll;
}

static inline u64 busy_loop_us_clock(void)
{
	return local_clock() >> 10;
}

static inline unsigned long sk_busy_loop_end_time(struct sock *sk)
{
	return busy_loop_us_clock() + ACCESS_ONCE(sk->sk_ll_usec);
}

/* in poll/select we use the global sysctl_net_ll_poll value */
static inline unsigned long busy_loop_end_time(void)
{
	return busy_loop_us_clock() + ACCESS_ONCE(sysctl_net_busy_poll);
}

static inline bool sk_can_busy_loop(const struct sock *sk)
{
	return sk->sk_ll_usec && sk->sk_napi_id && !signal_pending(current);
}


static inline bool busy_loop_timeout(unsigned long end_time)
{
	unsigned long now = busy_loop_us_clock();

	return time_after(now, end_time);
}

bool sk_busy_loop(struct sock *sk, int nonblock);

/* used in the NIC receive handler to mark the skb */
static inline void skb_mark_napi_id(struct sk_buff *skb,
				    struct napi_struct *napi)
{
	skb->napi_id = napi->napi_id;
}


#else /* CONFIG_NET_RX_BUSY_POLL */
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

static inline void skb_mark_napi_id(struct sk_buff *skb,
				    struct napi_struct *napi)
{
}

static inline bool busy_loop_timeout(unsigned long end_time)
{
	return true;
}

static inline bool sk_busy_loop(struct sock *sk, int nonblock)
{
	return false;
}

#endif /* CONFIG_NET_RX_BUSY_POLL */

/* used in the protocol hanlder to propagate the napi_id to the socket */
static inline void sk_mark_napi_id(struct sock *sk, const struct sk_buff *skb)
{
#ifdef CONFIG_NET_RX_BUSY_POLL
	sk->sk_napi_id = skb->napi_id;
#endif
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
