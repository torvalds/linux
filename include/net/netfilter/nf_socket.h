/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_SOCK_H_
#define _NF_SOCK_H_

#include <net/sock.h>

struct sock *nf_sk_lookup_slow_v4(struct net *net, const struct sk_buff *skb,
				  const struct net_device *indev);

struct sock *nf_sk_lookup_slow_v6(struct net *net, const struct sk_buff *skb,
				  const struct net_device *indev);

#endif
