// SPDX-License-Identifier: GPL-2.0-only
/* Kernel module to match one of a list of TCP/UDP(-Lite)/SCTP/DCCP ports:
   ports are in the same place so we can treat them as equal. */

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/types.h>
#include <linux/udp.h>
#include <linux/skbuff.h>
#include <linux/in.h>

#include <linux/netfilter/xt_multiport.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv6/ip6_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Netfilter Core Team <coreteam@netfilter.org>");
MODULE_DESCRIPTION("Xtables: multiple port matching for TCP, UDP, UDP-Lite, SCTP and DCCP");
MODULE_ALIAS("ipt_multiport");
MODULE_ALIAS("ip6t_multiport");

/* Returns 1 if the port is matched by the test, 0 otherwise. */
static inline bool
ports_match_v1(const struct xt_multiport_v1 *minfo,
	       u_int16_t src, u_int16_t dst)
{
	unsigned int i;
	u_int16_t s, e;

	for (i = 0; i < minfo->count; i++) {
		s = minfo->ports[i];

		if (minfo->pflags[i]) {
			/* range port matching */
			e = minfo->ports[++i];
			pr_debug("src or dst matches with %d-%d?\n", s, e);

			switch (minfo->flags) {
			case XT_MULTIPORT_SOURCE:
				if (src >= s && src <= e)
					return true ^ minfo->invert;
				break;
			case XT_MULTIPORT_DESTINATION:
				if (dst >= s && dst <= e)
					return true ^ minfo->invert;
				break;
			case XT_MULTIPORT_EITHER:
				if ((dst >= s && dst <= e) ||
				    (src >= s && src <= e))
					return true ^ minfo->invert;
				break;
			default:
				break;
			}
		} else {
			/* exact port matching */
			pr_debug("src or dst matches with %d?\n", s);

			switch (minfo->flags) {
			case XT_MULTIPORT_SOURCE:
				if (src == s)
					return true ^ minfo->invert;
				break;
			case XT_MULTIPORT_DESTINATION:
				if (dst == s)
					return true ^ minfo->invert;
				break;
			case XT_MULTIPORT_EITHER:
				if (src == s || dst == s)
					return true ^ minfo->invert;
				break;
			default:
				break;
			}
		}
	}

	return minfo->invert;
}

static bool
multiport_mt(const struct sk_buff *skb, struct xt_action_param *par)
{
	const __be16 *pptr;
	__be16 _ports[2];
	const struct xt_multiport_v1 *multiinfo = par->matchinfo;

	if (par->fragoff != 0)
		return false;

	pptr = skb_header_pointer(skb, par->thoff, sizeof(_ports), _ports);
	if (pptr == NULL) {
		/* We've been asked to examine this packet, and we
		 * can't.  Hence, no choice but to drop.
		 */
		pr_debug("Dropping evil offset=0 tinygram.\n");
		par->hotdrop = true;
		return false;
	}

	return ports_match_v1(multiinfo, ntohs(pptr[0]), ntohs(pptr[1]));
}

static inline bool
check(u_int16_t proto,
      u_int8_t ip_invflags,
      u_int8_t match_flags,
      u_int8_t count)
{
	/* Must specify supported protocol, no unknown flags or bad count */
	return (proto == IPPROTO_TCP || proto == IPPROTO_UDP
		|| proto == IPPROTO_UDPLITE
		|| proto == IPPROTO_SCTP || proto == IPPROTO_DCCP)
		&& !(ip_invflags & XT_INV_PROTO)
		&& (match_flags == XT_MULTIPORT_SOURCE
		    || match_flags == XT_MULTIPORT_DESTINATION
		    || match_flags == XT_MULTIPORT_EITHER)
		&& count <= XT_MULTI_PORTS;
}

static int multiport_mt_check(const struct xt_mtchk_param *par)
{
	const struct ipt_ip *ip = par->entryinfo;
	const struct xt_multiport_v1 *multiinfo = par->matchinfo;

	return check(ip->proto, ip->invflags, multiinfo->flags,
		     multiinfo->count) ? 0 : -EINVAL;
}

static int multiport_mt6_check(const struct xt_mtchk_param *par)
{
	const struct ip6t_ip6 *ip = par->entryinfo;
	const struct xt_multiport_v1 *multiinfo = par->matchinfo;

	return check(ip->proto, ip->invflags, multiinfo->flags,
		     multiinfo->count) ? 0 : -EINVAL;
}

static struct xt_match multiport_mt_reg[] __read_mostly = {
	{
		.name		= "multiport",
		.family		= NFPROTO_IPV4,
		.revision	= 1,
		.checkentry	= multiport_mt_check,
		.match		= multiport_mt,
		.matchsize	= sizeof(struct xt_multiport_v1),
		.me		= THIS_MODULE,
	},
	{
		.name		= "multiport",
		.family		= NFPROTO_IPV6,
		.revision	= 1,
		.checkentry	= multiport_mt6_check,
		.match		= multiport_mt,
		.matchsize	= sizeof(struct xt_multiport_v1),
		.me		= THIS_MODULE,
	},
};

static int __init multiport_mt_init(void)
{
	return xt_register_matches(multiport_mt_reg,
	       ARRAY_SIZE(multiport_mt_reg));
}

static void __exit multiport_mt_exit(void)
{
	xt_unregister_matches(multiport_mt_reg, ARRAY_SIZE(multiport_mt_reg));
}

module_init(multiport_mt_init);
module_exit(multiport_mt_exit);
