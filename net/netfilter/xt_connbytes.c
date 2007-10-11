/* Kernel module to match connection tracking byte counter.
 * GPL (C) 2002 Martin Devera (devik@cdi.cz).
 */
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_connbytes.h>
#include <net/netfilter/nf_conntrack.h>

#include <asm/div64.h>
#include <asm/bitops.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_DESCRIPTION("iptables match for matching number of pkts/bytes per connection");
MODULE_ALIAS("ipt_connbytes");
MODULE_ALIAS("ip6t_connbytes");

static bool
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const struct xt_match *match,
      const void *matchinfo,
      int offset,
      unsigned int protoff,
      bool *hotdrop)
{
	const struct xt_connbytes_info *sinfo = matchinfo;
	const struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	u_int64_t what = 0;	/* initialize to make gcc happy */
	u_int64_t bytes = 0;
	u_int64_t pkts = 0;
	const struct ip_conntrack_counter *counters;

	ct = nf_ct_get(skb, &ctinfo);
	if (!ct)
		return false;
	counters = ct->counters;

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
			what = div64_64(bytes, pkts);
		break;
	}

	if (sinfo->count.to)
		return what <= sinfo->count.to && what >= sinfo->count.from;
	else
		return what >= sinfo->count.from;
}

static bool check(const char *tablename,
		  const void *ip,
		  const struct xt_match *match,
		  void *matchinfo,
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
				    "proto=%d\n", match->family);
		return false;
	}

	return true;
}

static void
destroy(const struct xt_match *match, void *matchinfo)
{
	nf_ct_l3proto_module_put(match->family);
}

static struct xt_match xt_connbytes_match[] __read_mostly = {
	{
		.name		= "connbytes",
		.family		= AF_INET,
		.checkentry	= check,
		.match		= match,
		.destroy	= destroy,
		.matchsize	= sizeof(struct xt_connbytes_info),
		.me		= THIS_MODULE
	},
	{
		.name		= "connbytes",
		.family		= AF_INET6,
		.checkentry	= check,
		.match		= match,
		.destroy	= destroy,
		.matchsize	= sizeof(struct xt_connbytes_info),
		.me		= THIS_MODULE
	},
};

static int __init xt_connbytes_init(void)
{
	return xt_register_matches(xt_connbytes_match,
				   ARRAY_SIZE(xt_connbytes_match));
}

static void __exit xt_connbytes_fini(void)
{
	xt_unregister_matches(xt_connbytes_match,
			      ARRAY_SIZE(xt_connbytes_match));
}

module_init(xt_connbytes_init);
module_exit(xt_connbytes_fini);
