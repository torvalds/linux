/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_SOCK_H_
#define _NF_SOCK_H_

#include <net/sock.h>
#include <net/inet_timewait_sock.h>

static inline bool nf_sk_is_transparent(struct sock *sk)
{
	switch (sk->sk_state) {
	case TCP_TIME_WAIT:
		return inet_twsk(sk)->tw_transparent;
	case TCP_NEW_SYN_RECV:
		return inet_rsk(inet_reqsk(sk))->no_srccheck;
	default:
		return inet_sk(sk)->transparent;
	}
}

struct sock *nf_sk_lookup_slow_v4(struct net *net, const struct sk_buff *skb,
				  const struct net_device *indev);

struct sock *nf_sk_lookup_slow_v6(struct net *net, const struct sk_buff *skb,
				  const struct net_device *indev);

#endif
