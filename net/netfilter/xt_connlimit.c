/*
 * netfilter module to limit the number of parallel tcp
 * connections per IP address.
 *   (c) 2000 Gerd Knorr <kraxel@bytesex.org>
 *   Nov 2002: Martin Bene <martin.bene@icomedias.com>:
 *		only ignore TIME_WAIT or gone connections
 *   (C) CC Computer Consultants GmbH, 2007
 *
 * based on ...
 *
 * Kernel module to match connection tracking information.
 * GPL (C) 1999  Rusty Russell (rusty@rustcorp.com.au).
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_connlimit.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_tuple.h>
#include <net/netfilter/nf_conntrack_zones.h>
#include <net/netfilter/nf_conntrack_count.h>

static bool
connlimit_mt(const struct sk_buff *skb, struct xt_action_param *par)
{
	struct net *net = xt_net(par);
	const struct xt_connlimit_info *info = par->matchinfo;
	struct nf_conntrack_tuple tuple;
	const struct nf_conntrack_tuple *tuple_ptr = &tuple;
	const struct nf_conntrack_zone *zone = &nf_ct_zone_dflt;
	enum ip_conntrack_info ctinfo;
	const struct nf_conn *ct;
	unsigned int connections;
	u32 key[5];

	ct = nf_ct_get(skb, &ctinfo);
	if (ct != NULL) {
		tuple_ptr = &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple;
		zone = nf_ct_zone(ct);
	} else if (!nf_ct_get_tuplepr(skb, skb_network_offset(skb),
				      xt_family(par), net, &tuple)) {
		goto hotdrop;
	}

	if (xt_family(par) == NFPROTO_IPV6) {
		const struct ipv6hdr *iph = ipv6_hdr(skb);
		union nf_inet_addr addr;
		unsigned int i;

		memcpy(&addr.ip6, (info->flags & XT_CONNLIMIT_DADDR) ?
		       &iph->daddr : &iph->saddr, sizeof(addr.ip6));

		for (i = 0; i < ARRAY_SIZE(addr.ip6); ++i)
			addr.ip6[i] &= info->mask.ip6[i];
		memcpy(key, &addr, sizeof(addr.ip6));
		key[4] = zone->id;
	} else {
		const struct iphdr *iph = ip_hdr(skb);
		key[0] = (info->flags & XT_CONNLIMIT_DADDR) ?
			  iph->daddr : iph->saddr;

		key[0] &= info->mask.ip;
		key[1] = zone->id;
	}

	connections = nf_conncount_count(net, info->data, key, tuple_ptr,
					 zone);
	if (connections == 0)
		/* kmalloc failed, drop it entirely */
		goto hotdrop;

	return (connections > info->limit) ^ !!(info->flags & XT_CONNLIMIT_INVERT);

 hotdrop:
	par->hotdrop = true;
	return false;
}

static int connlimit_mt_check(const struct xt_mtchk_param *par)
{
	struct xt_connlimit_info *info = par->matchinfo;
	unsigned int keylen;

	keylen = sizeof(u32);
	if (par->family == NFPROTO_IPV6)
		keylen += sizeof(struct in6_addr);
	else
		keylen += sizeof(struct in_addr);

	/* init private data */
	info->data = nf_conncount_init(par->net, par->family, keylen);

	return PTR_ERR_OR_ZERO(info->data);
}

static void connlimit_mt_destroy(const struct xt_mtdtor_param *par)
{
	const struct xt_connlimit_info *info = par->matchinfo;

	nf_conncount_destroy(par->net, par->family, info->data);
}

static struct xt_match connlimit_mt_reg __read_mostly = {
	.name       = "connlimit",
	.revision   = 1,
	.family     = NFPROTO_UNSPEC,
	.checkentry = connlimit_mt_check,
	.match      = connlimit_mt,
	.matchsize  = sizeof(struct xt_connlimit_info),
	.usersize   = offsetof(struct xt_connlimit_info, data),
	.destroy    = connlimit_mt_destroy,
	.me         = THIS_MODULE,
};

static int __init connlimit_mt_init(void)
{
	return xt_register_match(&connlimit_mt_reg);
}

static void __exit connlimit_mt_exit(void)
{
	xt_unregister_match(&connlimit_mt_reg);
}

module_init(connlimit_mt_init);
module_exit(connlimit_mt_exit);
MODULE_AUTHOR("Jan Engelhardt <jengelh@medozas.de>");
MODULE_DESCRIPTION("Xtables: Number of connections matching");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_connlimit");
MODULE_ALIAS("ip6t_connlimit");
