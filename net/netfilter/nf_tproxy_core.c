/*
 * Transparent proxy support for Linux/iptables
 *
 * Copyright (c) 2006-2007 BalaBit IT Ltd.
 * Author: Balazs Scheidler, Krisztian Kovacs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>

#include <linux/net.h>
#include <linux/if.h>
#include <linux/netdevice.h>
#include <net/udp.h>
#include <net/netfilter/nf_tproxy_core.h>


static void
nf_tproxy_destructor(struct sk_buff *skb)
{
	struct sock *sk = skb->sk;

	skb->sk = NULL;
	skb->destructor = NULL;

	if (sk)
		sock_put(sk);
}

/* consumes sk */
void
nf_tproxy_assign_sock(struct sk_buff *skb, struct sock *sk)
{
	/* assigning tw sockets complicates things; most
	 * skb->sk->X checks would have to test sk->sk_state first */
	if (sk->sk_state == TCP_TIME_WAIT) {
		inet_twsk_put(inet_twsk(sk));
		return;
	}

	skb_orphan(skb);
	skb->sk = sk;
	skb->destructor = nf_tproxy_destructor;
}
EXPORT_SYMBOL_GPL(nf_tproxy_assign_sock);

static int __init nf_tproxy_init(void)
{
	pr_info("NF_TPROXY: Transparent proxy support initialized, version 4.1.0\n");
	pr_info("NF_TPROXY: Copyright (c) 2006-2007 BalaBit IT Ltd.\n");
	return 0;
}

module_init(nf_tproxy_init);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Krisztian Kovacs");
MODULE_DESCRIPTION("Transparent proxy support core routines");
