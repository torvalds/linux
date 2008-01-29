/*
 *	xt_iprange - Netfilter module to match IP address ranges
 *
 *	(C) 2003 Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>
 *	(C) CC Computer Consultants GmbH, 2008
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License version 2 as
 *	published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_ipv4/ipt_iprange.h>

static bool
iprange_mt_v0(const struct sk_buff *skb, const struct net_device *in,
              const struct net_device *out, const struct xt_match *match,
              const void *matchinfo, int offset, unsigned int protoff,
              bool *hotdrop)
{
	const struct ipt_iprange_info *info = matchinfo;
	const struct iphdr *iph = ip_hdr(skb);

	if (info->flags & IPRANGE_SRC) {
		if ((ntohl(iph->saddr) < ntohl(info->src.min_ip)
			  || ntohl(iph->saddr) > ntohl(info->src.max_ip))
			 ^ !!(info->flags & IPRANGE_SRC_INV)) {
			pr_debug("src IP %u.%u.%u.%u NOT in range %s"
				 "%u.%u.%u.%u-%u.%u.%u.%u\n",
				 NIPQUAD(iph->saddr),
				 info->flags & IPRANGE_SRC_INV ? "(INV) " : "",
				 NIPQUAD(info->src.min_ip),
				 NIPQUAD(info->src.max_ip));
			return false;
		}
	}
	if (info->flags & IPRANGE_DST) {
		if ((ntohl(iph->daddr) < ntohl(info->dst.min_ip)
			  || ntohl(iph->daddr) > ntohl(info->dst.max_ip))
			 ^ !!(info->flags & IPRANGE_DST_INV)) {
			pr_debug("dst IP %u.%u.%u.%u NOT in range %s"
				 "%u.%u.%u.%u-%u.%u.%u.%u\n",
				 NIPQUAD(iph->daddr),
				 info->flags & IPRANGE_DST_INV ? "(INV) " : "",
				 NIPQUAD(info->dst.min_ip),
				 NIPQUAD(info->dst.max_ip));
			return false;
		}
	}
	return true;
}

static bool
iprange_mt4(const struct sk_buff *skb, const struct net_device *in,
            const struct net_device *out, const struct xt_match *match,
            const void *matchinfo, int offset, unsigned int protoff,
            bool *hotdrop)
{
	const struct xt_iprange_mtinfo *info = matchinfo;
	const struct iphdr *iph = ip_hdr(skb);
	bool m;

	if (info->flags & IPRANGE_SRC) {
		m  = ntohl(iph->saddr) < ntohl(info->src_min.ip);
		m |= ntohl(iph->saddr) > ntohl(info->src_max.ip);
		m ^= info->flags & IPRANGE_SRC_INV;
		if (m) {
			pr_debug("src IP " NIPQUAD_FMT " NOT in range %s"
			         NIPQUAD_FMT "-" NIPQUAD_FMT "\n",
			         NIPQUAD(iph->saddr),
			         (info->flags & IPRANGE_SRC_INV) ? "(INV) " : "",
			         NIPQUAD(info->src_max.ip),
			         NIPQUAD(info->src_max.ip));
			return false;
		}
	}
	if (info->flags & IPRANGE_DST) {
		m  = ntohl(iph->daddr) < ntohl(info->dst_min.ip);
		m |= ntohl(iph->daddr) > ntohl(info->dst_max.ip);
		m ^= info->flags & IPRANGE_DST_INV;
		if (m) {
			pr_debug("dst IP " NIPQUAD_FMT " NOT in range %s"
			         NIPQUAD_FMT "-" NIPQUAD_FMT "\n",
			         NIPQUAD(iph->daddr),
			         (info->flags & IPRANGE_DST_INV) ? "(INV) " : "",
			         NIPQUAD(info->dst_min.ip),
			         NIPQUAD(info->dst_max.ip));
			return false;
		}
	}
	return true;
}

static inline int
iprange_ipv6_sub(const struct in6_addr *a, const struct in6_addr *b)
{
	unsigned int i;
	int r;

	for (i = 0; i < 4; ++i) {
		r = a->s6_addr32[i] - b->s6_addr32[i];
		if (r != 0)
			return r;
	}

	return 0;
}

static bool
iprange_mt6(const struct sk_buff *skb, const struct net_device *in,
            const struct net_device *out, const struct xt_match *match,
            const void *matchinfo, int offset, unsigned int protoff,
            bool *hotdrop)
{
	const struct xt_iprange_mtinfo *info = matchinfo;
	const struct ipv6hdr *iph = ipv6_hdr(skb);
	bool m;

	if (info->flags & IPRANGE_SRC) {
		m  = iprange_ipv6_sub(&iph->saddr, &info->src_min.in6) < 0;
		m |= iprange_ipv6_sub(&iph->saddr, &info->src_max.in6) > 0;
		m ^= info->flags & IPRANGE_SRC_INV;
		if (m)
			return false;
	}
	if (info->flags & IPRANGE_DST) {
		m  = iprange_ipv6_sub(&iph->daddr, &info->dst_min.in6) < 0;
		m |= iprange_ipv6_sub(&iph->daddr, &info->dst_max.in6) > 0;
		m ^= info->flags & IPRANGE_DST_INV;
		if (m)
			return false;
	}
	return true;
}

static struct xt_match iprange_mt_reg[] __read_mostly = {
	{
		.name      = "iprange",
		.revision  = 0,
		.family    = AF_INET,
		.match     = iprange_mt_v0,
		.matchsize = sizeof(struct ipt_iprange_info),
		.me        = THIS_MODULE,
	},
	{
		.name      = "iprange",
		.revision  = 1,
		.family    = AF_INET6,
		.match     = iprange_mt4,
		.matchsize = sizeof(struct xt_iprange_mtinfo),
		.me        = THIS_MODULE,
	},
	{
		.name      = "iprange",
		.revision  = 1,
		.family    = AF_INET6,
		.match     = iprange_mt6,
		.matchsize = sizeof(struct xt_iprange_mtinfo),
		.me        = THIS_MODULE,
	},
};

static int __init iprange_mt_init(void)
{
	return xt_register_matches(iprange_mt_reg, ARRAY_SIZE(iprange_mt_reg));
}

static void __exit iprange_mt_exit(void)
{
	xt_unregister_matches(iprange_mt_reg, ARRAY_SIZE(iprange_mt_reg));
}

module_init(iprange_mt_init);
module_exit(iprange_mt_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>, Jan Engelhardt <jengelh@computergmbh.de>");
MODULE_DESCRIPTION("Xtables: arbitrary IPv4 range matching");
