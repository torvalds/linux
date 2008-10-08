/* Kernel module to match connection tracking byte counter.
 * GPL (C) 2002 Martin Devera (devik@cdi.cz).
 */
#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/skbuff.h>
#include <linux/math64.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_connbytes.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_acct.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_DESCRIPTION("Xtables: Number of packets/bytes per connection matching");
MODULE_ALIAS("ipt_connbytes");
MODULE_ALIAS("ip6t_connbytes");

static bool
connbytes_mt(const struct sk_buff *skb, const struct net_device *in,
             const struct net_device *out, const struct xt_match *match,
             const void *matchinfo, int offset, unsigned int protoff,
             bool *hotdrop)
{
	const struct xt_connbytes_info *sinfo = matchinfo;
	const struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	u_int64_t what = 0;	/* initialize to make gcc happy */
	u_int64_t bytes = 0;
	u_int64_t pkts = 0;
	const struct nf_conn_counter *counters;

	ct = nf_ct_get(skb, &ctinfo);
	if (!ct)
		return false;

	counters = nf_conn_acct_find(ct);
	if (!counters)
		return false;

	switch (sinfo->what) {
	case XT_CONNBYTES_PKTS:
		switch (sinfo->direction) {
		case XT_CONNBYTES_DIR_ORIGINAL:
			what = counters[IP_CT_DIR_ORIGINAL].packets;
			break;
		case XT_CONNBYTES_DIR_REPLY:
			what = counters[IP_CT_DIR_REPLY].packets;
			break;
		case XT_CONNBYTES_DIR_BOTH:
			what = counters[IP_CT_DIR_ORIGINAL].packets;
			what += counters[IP_CT_DIR_REPLY].packets;
			break;
		}
		break;
	case XT_CONNBYTES_BYTES:
		switch (sinfo->direction) {
		case XT_CONNBYTES_DIR_ORIGINAL:
			what = counters[IP_CT_DIR_ORIGINAL].bytes;
			break;
		case XT_CONNBYTES_DIR_REPLY:
			what = counters[IP_CT_DIR_REPLY].bytes;
			break;
		case XT_CONNBYTES_DIR_BOTH:
			what = counters[IP_CT_DIR_ORIGINAL].bytes;
			what += counters[IP_CT_DIR_REPLY].bytes;
			break;
		}
		break;
	case XT_CONNBYTES_AVGPKT:
		switch (sinfo->direction) {
		case XT_CONNBYTES_DIR_ORIGINAL:
			bytes = counters[IP_CT_DIR_ORIGINAL].bytes;
			pkts  = counters[IP_CT_DIR_ORIGINAL].packets;
			break;
		case XT_CONNBYTES_DIR_REPLY:
			bytes = counters[IP_CT_DIR_REPLY].bytes;
			pkts  = counters[IP_CT_DIR_REPLY].packets;
			break;
		case XT_CONNBYTES_DIR_BOTH:
			bytes = counters[IP_CT_DIR_ORIGINAL].bytes +
				counters[IP_CT_DIR_REPLY].bytes;
			pkts  = counters[IP_CT_DIR_ORIGINAL].packets +
				counters[IP_CT_DIR_REPLY].packets;
			break;
		}
		if (pkts != 0)
			what = div64_u64(bytes, pkts);
		break;
	}

	if (sinfo->count.to)
		return what <= sinfo->count.to && what >= sinfo->count.from;
	else
		return what >= sinfo->count.from;
}

static bool
connbytes_mt_check(const char *tablename, const void *ip,
                   const struct xt_match *match, void *matchinfo,
                   unsigned int hook_mask)
{
	const struct xt_connbytes_info *sinfo = matchinfo;

	if (sinfo->what != XT_CONNBYTES_PKTS &&
	    sinfo->what != XT_CONNBYTES_BYTES &&
	    sinfo->what != XT_CONNBYTES_AVGPKT)
		return false;

	if (sinfo->direction != XT_CONNBYTES_DIR_ORIGINAL &&
	    sinfo->direction != XT_CONNBYTES_DIR_REPLY &&
	    sinfo->direction != XT_CONNBYTES_DIR_BOTH)
		return false;

	if (nf_ct_l3proto_try_module_get(match->family) < 0) {
		printk(KERN_WARNING "can't load conntrack support for "
				    "proto=%u\n", match->family);
		return false;
	}

	return true;
}

static void
connbytes_mt_destroy(const struct xt_match *match, void *matchinfo)
{
	nf_ct_l3proto_module_put(match->family);
}

static struct xt_match connbytes_mt_reg[] __read_mostly = {
	{
		.name		= "connbytes",
		.family		= NFPROTO_IPV4,
		.checkentry	= connbytes_mt_check,
		.match		= connbytes_mt,
		.destroy	= connbytes_mt_destroy,
		.matchsize	= sizeof(struct xt_connbytes_info),
		.me		= THIS_MODULE
	},
	{
		.name		= "connbytes",
		.family		= NFPROTO_IPV6,
		.checkentry	= connbytes_mt_check,
		.match		= connbytes_mt,
		.destroy	= connbytes_mt_destroy,
		.matchsize	= sizeof(struct xt_connbytes_info),
		.me		= THIS_MODULE
	},
};

static int __init connbytes_mt_init(void)
{
	return xt_register_matches(connbytes_mt_reg,
	       ARRAY_SIZE(connbytes_mt_reg));
}

static void __exit connbytes_mt_exit(void)
{
	xt_unregister_matches(connbytes_mt_reg, ARRAY_SIZE(connbytes_mt_reg));
}

module_init(connbytes_mt_init);
module_exit(connbytes_mt_exit);
