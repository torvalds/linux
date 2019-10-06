// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013 Patrick McHardy <kaber@trash.net>
 */

#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_SYNPROXY.h>

#include <net/netfilter/nf_synproxy.h>

static unsigned int
synproxy_tg6(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct xt_synproxy_info *info = par->targinfo;
	struct net *net = xt_net(par);
	struct synproxy_net *snet = synproxy_pernet(net);
	struct synproxy_options opts = {};
	struct tcphdr *th, _th;

	if (nf_ip6_checksum(skb, xt_hooknum(par), par->thoff, IPPROTO_TCP))
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
		opts.mss_encode = opts.mss_option;
		opts.mss_option = info->mss;
		if (opts.options & XT_SYNPROXY_OPT_TIMESTAMP)
			synproxy_init_timestamp_cookie(info, &opts);
		else
			opts.options &= ~(XT_SYNPROXY_OPT_WSCALE |
					  XT_SYNPROXY_OPT_SACK_PERM |
					  XT_SYNPROXY_OPT_ECN);

		synproxy_send_client_synack_ipv6(net, skb, th, &opts);
		consume_skb(skb);
		return NF_STOLEN;

	} else if (th->ack && !(th->fin || th->rst || th->syn)) {
		/* ACK from client */
		if (synproxy_recv_client_ack_ipv6(net, skb, th, &opts,
						  ntohl(th->seq))) {
			consume_skb(skb);
			return NF_STOLEN;
		} else {
			return NF_DROP;
		}
	}

	return XT_CONTINUE;
}

static int synproxy_tg6_check(const struct xt_tgchk_param *par)
{
	struct synproxy_net *snet = synproxy_pernet(par->net);
	const struct ip6t_entry *e = par->entryinfo;
	int err;

	if (!(e->ipv6.flags & IP6T_F_PROTO) ||
	    e->ipv6.proto != IPPROTO_TCP ||
	    e->ipv6.invflags & XT_INV_PROTO)
		return -EINVAL;

	err = nf_ct_netns_get(par->net, par->family);
	if (err)
		return err;

	err = nf_synproxy_ipv6_init(snet, par->net);
	if (err) {
		nf_ct_netns_put(par->net, par->family);
		return err;
	}

	return err;
}

static void synproxy_tg6_destroy(const struct xt_tgdtor_param *par)
{
	struct synproxy_net *snet = synproxy_pernet(par->net);

	nf_synproxy_ipv6_fini(snet, par->net);
	nf_ct_netns_put(par->net, par->family);
}

static struct xt_target synproxy_tg6_reg __read_mostly = {
	.name		= "SYNPROXY",
	.family		= NFPROTO_IPV6,
	.hooks		= (1 << NF_INET_LOCAL_IN) | (1 << NF_INET_FORWARD),
	.target		= synproxy_tg6,
	.targetsize	= sizeof(struct xt_synproxy_info),
	.checkentry	= synproxy_tg6_check,
	.destroy	= synproxy_tg6_destroy,
	.me		= THIS_MODULE,
};

static int __init synproxy_tg6_init(void)
{
	return xt_register_target(&synproxy_tg6_reg);
}

static void __exit synproxy_tg6_exit(void)
{
	xt_unregister_target(&synproxy_tg6_reg);
}

module_init(synproxy_tg6_init);
module_exit(synproxy_tg6_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
