/* Kernel module to match connection tracking byte counter.
 * GPL (C) 2002 Martin Devera (devik@cdi.cz).
 *
 * 2004-07-20 Harald Welte <laforge@netfilter.org>
 * 	- reimplemented to use per-connection accounting counters
 * 	- add functionality to match number of packets
 * 	- add functionality to match average packet size
 * 	- add support to match directions seperately
 * 2005-10-16 Harald Welte <laforge@netfilter.org>
 * 	- Port to x_tables
 *
 */
#include <linux/module.h>
#include <linux/skbuff.h>
#include <net/netfilter/nf_conntrack_compat.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_connbytes.h>

#include <asm/div64.h>
#include <asm/bitops.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_DESCRIPTION("iptables match for matching number of pkts/bytes per connection");
MODULE_ALIAS("ipt_connbytes");

/* 64bit divisor, dividend and result. dynamic precision */
static u_int64_t div64_64(u_int64_t dividend, u_int64_t divisor)
{
	u_int32_t d = divisor;

	if (divisor > 0xffffffffULL) {
		unsigned int shift = fls(divisor >> 32);

		d = divisor >> shift;
		dividend >>= shift;
	}

	do_div(dividend, d);
	return dividend;
}

static int
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const void *matchinfo,
      int offset,
      unsigned int protoff,
      int *hotdrop)
{
	const struct xt_connbytes_info *sinfo = matchinfo;
	u_int64_t what = 0;	/* initialize to make gcc happy */
	const struct ip_conntrack_counter *counters;

	if (!(counters = nf_ct_get_counters(skb)))
		return 0; /* no match */

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
			what = div64_64(counters[IP_CT_DIR_ORIGINAL].bytes,
					counters[IP_CT_DIR_ORIGINAL].packets);
			break;
		case XT_CONNBYTES_DIR_REPLY:
			what = div64_64(counters[IP_CT_DIR_REPLY].bytes,
					counters[IP_CT_DIR_REPLY].packets);
			break;
		case XT_CONNBYTES_DIR_BOTH:
			{
				u_int64_t bytes;
				u_int64_t pkts;
				bytes = counters[IP_CT_DIR_ORIGINAL].bytes +
					counters[IP_CT_DIR_REPLY].bytes;
				pkts = counters[IP_CT_DIR_ORIGINAL].packets+
					counters[IP_CT_DIR_REPLY].packets;

				/* FIXME_THEORETICAL: what to do if sum
				 * overflows ? */

				what = div64_64(bytes, pkts);
			}
			break;
		}
		break;
	}

	if (sinfo->count.to)
		return (what <= sinfo->count.to && what >= sinfo->count.from);
	else
		return (what >= sinfo->count.from);
}

static int check(const char *tablename,
		 const void *ip,
		 void *matchinfo,
		 unsigned int matchsize,
		 unsigned int hook_mask)
{
	const struct xt_connbytes_info *sinfo = matchinfo;

	if (matchsize != XT_ALIGN(sizeof(struct xt_connbytes_info)))
		return 0;

	if (sinfo->what != XT_CONNBYTES_PKTS &&
	    sinfo->what != XT_CONNBYTES_BYTES &&
	    sinfo->what != XT_CONNBYTES_AVGPKT)
		return 0;

	if (sinfo->direction != XT_CONNBYTES_DIR_ORIGINAL &&
	    sinfo->direction != XT_CONNBYTES_DIR_REPLY &&
	    sinfo->direction != XT_CONNBYTES_DIR_BOTH)
		return 0;

	return 1;
}

static struct xt_match connbytes_match = {
	.name		= "connbytes",
	.match		= &match,
	.checkentry	= &check,
	.me		= THIS_MODULE
};
static struct xt_match connbytes6_match = {
	.name		= "connbytes",
	.match		= &match,
	.checkentry	= &check,
	.me		= THIS_MODULE
};

static int __init init(void)
{
	int ret;
	ret = xt_register_match(AF_INET, &connbytes_match);
	if (ret)
		return ret;

	ret = xt_register_match(AF_INET6, &connbytes6_match);
	if (ret)
		xt_unregister_match(AF_INET, &connbytes_match);
	return ret;
}

static void __exit fini(void)
{
	xt_unregister_match(AF_INET, &connbytes_match);
	xt_unregister_match(AF_INET6, &connbytes6_match);
}

module_init(init);
module_exit(fini);
