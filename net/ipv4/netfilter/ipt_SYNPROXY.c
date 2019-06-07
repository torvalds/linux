/*
 * Copyright (c) 2013 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_SYNPROXY.h>

#include <net/netfilter/nf_synproxy.h>

static unsigned int
synproxy_tg4(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct xt_synproxy_info *info = par->targinfo;
	struct net *net = xt_net(par);
	struct synproxy_net *snet = synproxy_pernet(net);
	struct synproxy_options opts = {};
	struct tcphdr *th, _th;

	if (nf_ip_checksum(skb, xt_hooknum(par), par->thoff, IPPROTO_TCP))
		return NF_DROP;

	th = skb_header_pointer(skb, par->thoff, sizeof(_th), &_th);
	if (th == NULL)
		return NF_DROP;

	if (!synproxy_parse_options(skb, par->thoff, th, &opts))
		return NF_DROP;

	if (th->syn && !(th->ack || th->fin || th->rst)) {
		/* Initial SYN from client */
		this_cpu_inc(snet->stats->syn_received);

		if (th->ece && th->cwr)
			opts.options |= XT_SYNPROXY_OPT_ECN;

		opts.options &= info->options;
		if (opts.options & XT_SYNPROXY_OPT_TIMESTAMP)
			synproxy_init_timestamp_cookie(info, &opts);
		else
			opts.options &= ~(XT_SYNPROXY_OPT_WSCALE |
					  XT_SYNPROXY_OPT_SACK_PERM |
					  XT_SYNPROXY_OPT_ECN);

		synproxy_send_client_synack(net, skb, th, &opts);
		consume_skb(skb);
		return NF_STOLEN;
	} else if (th->ack && !(th->fin || th->rst || th->syn)) {
		/* ACK from client */
		if (synproxy_recv_client_ack(net, skb, th, &opts, ntohl(th->seq))) {
			consume_skb(skb);
			return NF_STOLEN;
		} else {
			return NF_DROP;
		}
	}

	return XT_CONTINUE;
}

static int synproxy_tg4_check(const struct xt_tgchk_param *par)
{
	struct synproxy_net *snet = synproxy_pernet(par->net);
	const struct ipt_entry *e = par->entryinfo;
	int err;

	if (e->ip.proto != IPPROTO_TCP ||
	    e->ip.invflags & XT_INV_PROTO)
		return -EINVAL;

	err = nf_ct_netns_get(par->net, par->family);
	if (err)
		return err;

	err = nf_synproxy_ipv4_init(snet, par->net);
	if (err) {
		nf_ct_netns_put(par->net, par->family);
		return err;
	}

	snet->hook_ref4++;
	return err;
}

static void synproxy_tg4_destroy(const struct xt_tgdtor_param *par)
{
	struct synproxy_net *snet = synproxy_pernet(par->net);

	nf_synproxy_ipv4_fini(snet, par->net);
	nf_ct_netns_put(par->net, par->family);
}

static struct xt_target synproxy_tg4_reg __read_mostly = {
	.name		= "SYNPROXY",
	.family		= NFPROTO_IPV4,
	.hooks		= (1 << NF_INET_LOCAL_IN) | (1 << NF_INET_FORWARD),
	.target		= synproxy_tg4,
	.targetsize	= sizeof(struct xt_synproxy_info),
	.checkentry	= synproxy_tg4_check,
	.destroy	= synproxy_tg4_destroy,
	.me		= THIS_MODULE,
};

static int __init synproxy_tg4_init(void)
{
	return xt_register_target(&synproxy_tg4_reg);
}

static void __exit synproxy_tg4_exit(void)
{
	xt_unregister_target(&synproxy_tg4_reg);
}

module_init(synproxy_tg4_init);
module_exit(synproxy_tg4_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
