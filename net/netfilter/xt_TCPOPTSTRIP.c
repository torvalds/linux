// SPDX-License-Identifier: GPL-2.0-only
/*
 * A module for stripping a specific TCP option from TCP packets.
 *
 * Copyright (C) 2007 Sven Schnelle <svens@bitebene.org>
 * Copyright © CC Computer Consultants GmbH, 2007
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <net/ipv6.h>
#include <net/tcp.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_TCPOPTSTRIP.h>

static inline unsigned int optlen(const u_int8_t *opt, unsigned int offset)
{
	/* Beware zero-length options: make finite progress */
	if (opt[offset] <= TCPOPT_NOP || opt[offset+1] == 0)
		return 1;
	else
		return opt[offset+1];
}

static unsigned int
tcpoptstrip_mangle_packet(struct sk_buff *skb,
			  const struct xt_action_param *par,
			  unsigned int tcphoff)
{
	const struct xt_tcpoptstrip_target_info *info = par->targinfo;
	struct tcphdr *tcph, _th;
	unsigned int optl, i, j;
	u_int16_t n, o;
	u_int8_t *opt;
	int tcp_hdrlen;

	/* This is a fragment, no TCP header is available */
	if (par->fragoff != 0)
		return XT_CONTINUE;

	tcph = skb_header_pointer(skb, tcphoff, sizeof(_th), &_th);
	if (!tcph)
		return NF_DROP;

	tcp_hdrlen = tcph->doff * 4;
	if (tcp_hdrlen < sizeof(struct tcphdr))
		return NF_DROP;

	if (skb_ensure_writable(skb, tcphoff + tcp_hdrlen))
		return NF_DROP;

	/* must reload tcph, might have been moved */
	tcph = (struct tcphdr *)(skb_network_header(skb) + tcphoff);
	opt  = (u8 *)tcph;

	/*
	 * Walk through all TCP options - if we find some option to remove,
	 * set all octets to %TCPOPT_NOP and adjust checksum.
	 */
	for (i = sizeof(struct tcphdr); i < tcp_hdrlen - 1; i += optl) {
		optl = optlen(opt, i);

		if (i + optl > tcp_hdrlen)
			break;

		if (!tcpoptstrip_test_bit(info->strip_bmap, opt[i]))
			continue;

		for (j = 0; j < optl; ++j) {
			o = opt[i+j];
			n = TCPOPT_NOP;
			if ((i + j) % 2 == 0) {
				o <<= 8;
				n <<= 8;
			}
			inet_proto_csum_replace2(&tcph->check, skb, htons(o),
						 htons(n), false);
		}
		memset(opt + i, TCPOPT_NOP, optl);
	}

	return XT_CONTINUE;
}

static unsigned int
tcpoptstrip_tg4(struct sk_buff *skb, const struct xt_action_param *par)
{
	return tcpoptstrip_mangle_packet(skb, par, ip_hdrlen(skb));
}

#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
static unsigned int
tcpoptstrip_tg6(struct sk_buff *skb, const struct xt_action_param *par)
{
	struct ipv6hdr *ipv6h = ipv6_hdr(skb);
	int tcphoff;
	u_int8_t nexthdr;
	__be16 frag_off;

	nexthdr = ipv6h->nexthdr;
	tcphoff = ipv6_skip_exthdr(skb, sizeof(*ipv6h), &nexthdr, &frag_off);
	if (tcphoff < 0)
		return NF_DROP;

	return tcpoptstrip_mangle_packet(skb, par, tcphoff);
}
#endif

static struct xt_target tcpoptstrip_tg_reg[] __read_mostly = {
	{
		.name       = "TCPOPTSTRIP",
		.family     = NFPROTO_IPV4,
		.table      = "mangle",
		.proto      = IPPROTO_TCP,
		.target     = tcpoptstrip_tg4,
		.targetsize = sizeof(struct xt_tcpoptstrip_target_info),
		.me         = THIS_MODULE,
	},
#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
	{
		.name       = "TCPOPTSTRIP",
		.family     = NFPROTO_IPV6,
		.table      = "mangle",
		.proto      = IPPROTO_TCP,
		.target     = tcpoptstrip_tg6,
		.targetsize = sizeof(struct xt_tcpoptstrip_target_info),
		.me         = THIS_MODULE,
	},
#endif
};

static int __init tcpoptstrip_tg_init(void)
{
	return xt_register_targets(tcpoptstrip_tg_reg,
				   ARRAY_SIZE(tcpoptstrip_tg_reg));
}

static void __exit tcpoptstrip_tg_exit(void)
{
	xt_unregister_targets(tcpoptstrip_tg_reg,
			      ARRAY_SIZE(tcpoptstrip_tg_reg));
}

module_init(tcpoptstrip_tg_init);
module_exit(tcpoptstrip_tg_exit);
MODULE_AUTHOR("Sven Schnelle <svens@bitebene.org>, Jan Engelhardt <jengelh@medozas.de>");
MODULE_DESCRIPTION("Xtables: TCP option stripping");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_TCPOPTSTRIP");
MODULE_ALIAS("ip6t_TCPOPTSTRIP");
