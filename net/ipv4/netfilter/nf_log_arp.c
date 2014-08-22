/*
 * (C) 2014 by Pablo Neira Ayuso <pablo@netfilter.org>
 *
 * Based on code from ebt_log from:
 *
 * Bart De Schuymer <bdschuym@pandora.be>
 * Harald Welte <laforge@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/ip.h>
#include <net/route.h>

#include <linux/netfilter.h>
#include <linux/netfilter/xt_LOG.h>
#include <net/netfilter/nf_log.h>

static struct nf_loginfo default_loginfo = {
	.type	= NF_LOG_TYPE_LOG,
	.u = {
		.log = {
			.level	  = 5,
			.logflags = NF_LOG_MASK,
		},
	},
};

struct arppayload {
	unsigned char mac_src[ETH_ALEN];
	unsigned char ip_src[4];
	unsigned char mac_dst[ETH_ALEN];
	unsigned char ip_dst[4];
};

static void dump_arp_packet(struct nf_log_buf *m,
			    const struct nf_loginfo *info,
			    const struct sk_buff *skb, unsigned int nhoff)
{
	const struct arphdr *ah;
	struct arphdr _arph;
	const struct arppayload *ap;
	struct arppayload _arpp;

	ah = skb_header_pointer(skb, 0, sizeof(_arph), &_arph);
	if (ah == NULL) {
		nf_log_buf_add(m, "TRUNCATED");
		return;
	}
	nf_log_buf_add(m, "ARP HTYPE=%d PTYPE=0x%04x OPCODE=%d",
		       ntohs(ah->ar_hrd), ntohs(ah->ar_pro), ntohs(ah->ar_op));

	/* If it's for Ethernet and the lengths are OK, then log the ARP
	 * payload.
	 */
	if (ah->ar_hrd != htons(1) ||
	    ah->ar_hln != ETH_ALEN ||
	    ah->ar_pln != sizeof(__be32))
		return;

	ap = skb_header_pointer(skb, sizeof(_arph), sizeof(_arpp), &_arpp);
	if (ap == NULL) {
		nf_log_buf_add(m, " INCOMPLETE [%Zu bytes]",
			       skb->len - sizeof(_arph));
		return;
	}
	nf_log_buf_add(m, " MACSRC=%pM IPSRC=%pI4 MACDST=%pM IPDST=%pI4",
		       ap->mac_src, ap->ip_src, ap->mac_dst, ap->ip_dst);
}

void nf_log_arp_packet(struct net *net, u_int8_t pf,
		      unsigned int hooknum, const struct sk_buff *skb,
		      const struct net_device *in,
		      const struct net_device *out,
		      const struct nf_loginfo *loginfo,
		      const char *prefix)
{
	struct nf_log_buf *m;

	/* FIXME: Disabled from containers until syslog ns is supported */
	if (!net_eq(net, &init_net))
		return;

	m = nf_log_buf_open();

	if (!loginfo)
		loginfo = &default_loginfo;

	nf_log_dump_packet_common(m, pf, hooknum, skb, in, out, loginfo,
				  prefix);
	dump_arp_packet(m, loginfo, skb, 0);

	nf_log_buf_close(m);
}

static struct nf_logger nf_arp_logger __read_mostly = {
	.name		= "nf_log_arp",
	.type		= NF_LOG_TYPE_LOG,
	.logfn		= nf_log_arp_packet,
	.me		= THIS_MODULE,
};

static int __net_init nf_log_arp_net_init(struct net *net)
{
	nf_log_set(net, NFPROTO_ARP, &nf_arp_logger);
	return 0;
}

static void __net_exit nf_log_arp_net_exit(struct net *net)
{
	nf_log_unset(net, &nf_arp_logger);
}

static struct pernet_operations nf_log_arp_net_ops = {
	.init = nf_log_arp_net_init,
	.exit = nf_log_arp_net_exit,
};

static int __init nf_log_arp_init(void)
{
	int ret;

	ret = register_pernet_subsys(&nf_log_arp_net_ops);
	if (ret < 0)
		return ret;

	nf_log_register(NFPROTO_ARP, &nf_arp_logger);
	return 0;
}

static void __exit nf_log_arp_exit(void)
{
	unregister_pernet_subsys(&nf_log_arp_net_ops);
	nf_log_unregister(&nf_arp_logger);
}

module_init(nf_log_arp_init);
module_exit(nf_log_arp_exit);

MODULE_AUTHOR("Pablo Neira Ayuso <pablo@netfilter.org>");
MODULE_DESCRIPTION("Netfilter ARP packet logging");
MODULE_LICENSE("GPL");
MODULE_ALIAS_NF_LOGGER(3, 0);
