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
		nf_tproxy_put_sock(sk);
}

/* consumes sk */
int
nf_tproxy_assign_sock(struct sk_buff *skb, struct sock *sk)
{
	bool transparent = (sk->sk_state == TCP_TIME_WAIT) ?
				inet_twsk(sk)->tw_transparent :
				inet_sk(sk)->transparent;

	if (transparent) {
		skb_orphan(skb);
		skb->sk = sk;
		skb->destructor = nf_tproxy_destructor;
		return 1;
	} else
		nf_tproxy_put_sock(sk);

	return 0;
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
