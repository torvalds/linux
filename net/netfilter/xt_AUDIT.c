// SPDX-License-Identifier: GPL-2.0-only
/*
 * Creates audit record for dropped/accepted packets
 *
 * (C) 2010-2011 Thomas Graf <tgraf@redhat.com>
 * (C) 2010-2011 Red Hat, Inc.
*/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/audit.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/if_arp.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_AUDIT.h>
#include <linux/netfilter_bridge/ebtables.h>
#include <net/ipv6.h>
#include <net/ip.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Thomas Graf <tgraf@redhat.com>");
MODULE_DESCRIPTION("Xtables: creates audit records for dropped/accepted packets");
MODULE_ALIAS("ipt_AUDIT");
MODULE_ALIAS("ip6t_AUDIT");
MODULE_ALIAS("ebt_AUDIT");
MODULE_ALIAS("arpt_AUDIT");

static bool audit_ip4(struct audit_buffer *ab, struct sk_buff *skb)
{
	struct iphdr _iph;
	const struct iphdr *ih;

	ih = skb_header_pointer(skb, skb_network_offset(skb), sizeof(_iph), &_iph);
	if (!ih)
		return false;

	audit_log_format(ab, " saddr=%pI4 daddr=%pI4 proto=%hhu",
			 &ih->saddr, &ih->daddr, ih->protocol);

	return true;
}

static bool audit_ip6(struct audit_buffer *ab, struct sk_buff *skb)
{
	struct ipv6hdr _ip6h;
	const struct ipv6hdr *ih;
	u8 nexthdr;
	__be16 frag_off;

	ih = skb_header_pointer(skb, skb_network_offset(skb), sizeof(_ip6h), &_ip6h);
	if (!ih)
		return false;

	nexthdr = ih->nexthdr;
	ipv6_skip_exthdr(skb, skb_network_offset(skb) + sizeof(_ip6h), &nexthdr, &frag_off);

	audit_log_format(ab, " saddr=%pI6c daddr=%pI6c proto=%hhu",
			 &ih->saddr, &ih->daddr, nexthdr);

	return true;
}

static unsigned int
audit_tg(struct sk_buff *skb, const struct xt_action_param *par)
{
	struct audit_buffer *ab;
	int fam = -1;

	if (audit_enabled == AUDIT_OFF)
		goto errout;
	ab = audit_log_start(NULL, GFP_ATOMIC, AUDIT_NETFILTER_PKT);
	if (ab == NULL)
		goto errout;

	audit_log_format(ab, "mark=%#x", skb->mark);

	switch (xt_family(par)) {
	case NFPROTO_BRIDGE:
		switch (eth_hdr(skb)->h_proto) {
		case htons(ETH_P_IP):
			fam = audit_ip4(ab, skb) ? NFPROTO_IPV4 : -1;
			break;
		case htons(ETH_P_IPV6):
			fam = audit_ip6(ab, skb) ? NFPROTO_IPV6 : -1;
			break;
		}
		break;
	case NFPROTO_IPV4:
		fam = audit_ip4(ab, skb) ? NFPROTO_IPV4 : -1;
		break;
	case NFPROTO_IPV6:
		fam = audit_ip6(ab, skb) ? NFPROTO_IPV6 : -1;
		break;
	}

	if (fam == -1)
		audit_log_format(ab, " saddr=? daddr=? proto=-1");

	audit_log_end(ab);

errout:
	return XT_CONTINUE;
}

static unsigned int
audit_tg_ebt(struct sk_buff *skb, const struct xt_action_param *par)
{
	audit_tg(skb, par);
	return EBT_CONTINUE;
}

static int audit_tg_check(const struct xt_tgchk_param *par)
{
	const struct xt_audit_info *info = par->targinfo;

	if (info->type > XT_AUDIT_TYPE_MAX) {
		pr_info_ratelimited("Audit type out of range (valid range: 0..%hhu)\n",
				    XT_AUDIT_TYPE_MAX);
		return -ERANGE;
	}

	return 0;
}

static struct xt_target audit_tg_reg[] __read_mostly = {
	{
		.name		= "AUDIT",
		.family		= NFPROTO_UNSPEC,
		.target		= audit_tg,
		.targetsize	= sizeof(struct xt_audit_info),
		.checkentry	= audit_tg_check,
		.me		= THIS_MODULE,
	},
	{
		.name		= "AUDIT",
		.family		= NFPROTO_BRIDGE,
		.target		= audit_tg_ebt,
		.targetsize	= sizeof(struct xt_audit_info),
		.checkentry	= audit_tg_check,
		.me		= THIS_MODULE,
	},
};

static int __init audit_tg_init(void)
{
	return xt_register_targets(audit_tg_reg, ARRAY_SIZE(audit_tg_reg));
}

static void __exit audit_tg_exit(void)
{
	xt_unregister_targets(audit_tg_reg, ARRAY_SIZE(audit_tg_reg));
}

module_init(audit_tg_init);
module_exit(audit_tg_exit);
