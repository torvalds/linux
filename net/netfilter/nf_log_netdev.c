/*
 * (C) 2016 by Pablo Neira Ayuso <pablo@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <net/route.h>

#include <linux/netfilter.h>
#include <net/netfilter/nf_log.h>

static void nf_log_netdev_packet(struct net *net, u_int8_t pf,
				 unsigned int hooknum,
				 const struct sk_buff *skb,
				 const struct net_device *in,
				 const struct net_device *out,
				 const struct nf_loginfo *loginfo,
				 const char *prefix)
{
	nf_log_l2packet(net, pf, skb->protocol, hooknum, skb, in, out,
			loginfo, prefix);
}

static struct nf_logger nf_netdev_logger __read_mostly = {
	.name		= "nf_log_netdev",
	.type		= NF_LOG_TYPE_LOG,
	.logfn		= nf_log_netdev_packet,
	.me		= THIS_MODULE,
};

static int __net_init nf_log_netdev_net_init(struct net *net)
{
	return nf_log_set(net, NFPROTO_NETDEV, &nf_netdev_logger);
}

static void __net_exit nf_log_netdev_net_exit(struct net *net)
{
	nf_log_unset(net, &nf_netdev_logger);
}

static struct pernet_operations nf_log_netdev_net_ops = {
	.init = nf_log_netdev_net_init,
	.exit = nf_log_netdev_net_exit,
};

static int __init nf_log_netdev_init(void)
{
	int ret;

	/* Request to load the real packet loggers. */
	nf_logger_request_module(NFPROTO_IPV4, NF_LOG_TYPE_LOG);
	nf_logger_request_module(NFPROTO_IPV6, NF_LOG_TYPE_LOG);
	nf_logger_request_module(NFPROTO_ARP, NF_LOG_TYPE_LOG);

	ret = register_pernet_subsys(&nf_log_netdev_net_ops);
	if (ret < 0)
		return ret;

	nf_log_register(NFPROTO_NETDEV, &nf_netdev_logger);
	return 0;
}

static void __exit nf_log_netdev_exit(void)
{
	unregister_pernet_subsys(&nf_log_netdev_net_ops);
	nf_log_unregister(&nf_netdev_logger);
}

module_init(nf_log_netdev_init);
module_exit(nf_log_netdev_exit);

MODULE_AUTHOR("Pablo Neira Ayuso <pablo@netfilter.org>");
MODULE_DESCRIPTION("Netfilter netdev packet logging");
MODULE_LICENSE("GPL");
MODULE_ALIAS_NF_LOGGER(5, 0); /* NFPROTO_NETDEV */
