/* Same.  Just like SNAT, only try to make the connections
 * 	  between client A and server B always have the same source ip.
 *
 * (C) 2000 Paul `Rusty' Russell
 * (C) 2001 Martin Josefsson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/types.h>
#include <linux/ip.h>
#include <linux/timer.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/netdevice.h>
#include <linux/if.h>
#include <linux/inetdevice.h>
#include <net/protocol.h>
#include <net/checksum.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter/x_tables.h>
#include <net/netfilter/nf_nat_rule.h>
#include <linux/netfilter_ipv4/ipt_SAME.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Martin Josefsson <gandalf@wlug.westbo.se>");
MODULE_DESCRIPTION("iptables special SNAT module for consistent sourceip");

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

static int
same_check(const char *tablename,
	      const void *e,
	      const struct xt_target *target,
	      void *targinfo,
	      unsigned int hook_mask)
{
	unsigned int count, countess, rangeip, index = 0;
	struct ipt_same_info *mr = targinfo;

	mr->ipnum = 0;

	if (mr->rangesize < 1) {
		DEBUGP("same_check: need at least one dest range.\n");
		return 0;
	}
	if (mr->rangesize > IPT_SAME_MAX_RANGE) {
		DEBUGP("same_check: too many ranges specified, maximum "
				"is %u ranges\n",
				IPT_SAME_MAX_RANGE);
		return 0;
	}
	for (count = 0; count < mr->rangesize; count++) {
		if (ntohl(mr->range[count].min_ip) >
				ntohl(mr->range[count].max_ip)) {
			DEBUGP("same_check: min_ip is larger than max_ip in "
				"range `%u.%u.%u.%u-%u.%u.%u.%u'.\n",
				NIPQUAD(mr->range[count].min_ip),
				NIPQUAD(mr->range[count].max_ip));
			return 0;
		}
		if (!(mr->range[count].flags & IP_NAT_RANGE_MAP_IPS)) {
			DEBUGP("same_check: bad MAP_IPS.\n");
			return 0;
		}
		rangeip = (ntohl(mr->range[count].max_ip) -
					ntohl(mr->range[count].min_ip) + 1);
		mr->ipnum += rangeip;

		DEBUGP("same_check: range %u, ipnum = %u\n", count, rangeip);
	}
	DEBUGP("same_check: total ipaddresses = %u\n", mr->ipnum);

	mr->iparray = kmalloc((sizeof(u_int32_t) * mr->ipnum), GFP_KERNEL);
	if (!mr->iparray) {
		DEBUGP("same_check: Couldn't allocate %u bytes "
			"for %u ipaddresses!\n",
			(sizeof(u_int32_t) * mr->ipnum), mr->ipnum);
		return 0;
	}
	DEBUGP("same_check: Allocated %u bytes for %u ipaddresses.\n",
			(sizeof(u_int32_t) * mr->ipnum), mr->ipnum);

	for (count = 0; count < mr->rangesize; count++) {
		for (countess = ntohl(mr->range[count].min_ip);
				countess <= ntohl(mr->range[count].max_ip);
					countess++) {
			mr->iparray[index] = countess;
			DEBUGP("same_check: Added ipaddress `%u.%u.%u.%u' "
				"in index %u.\n",
				HIPQUAD(countess), index);
			index++;
		}
	}
	return 1;
}

static void
same_destroy(const struct xt_target *target, void *targinfo)
{
	struct ipt_same_info *mr = targinfo;

	kfree(mr->iparray);

	DEBUGP("same_destroy: Deallocated %u bytes for %u ipaddresses.\n",
			(sizeof(u_int32_t) * mr->ipnum), mr->ipnum);
}

static unsigned int
same_target(struct sk_buff **pskb,
		const struct net_device *in,
		const struct net_device *out,
		unsigned int hooknum,
		const struct xt_target *target,
		const void *targinfo)
{
	struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	u_int32_t tmpip, aindex;
	__be32 new_ip;
	const struct ipt_same_info *same = targinfo;
	struct nf_nat_range newrange;
	const struct nf_conntrack_tuple *t;

	NF_CT_ASSERT(hooknum == NF_IP_PRE_ROUTING ||
			hooknum == NF_IP_POST_ROUTING);
	ct = nf_ct_get(*pskb, &ctinfo);

	t = &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple;

	/* Base new source on real src ip and optionally dst ip,
	   giving some hope for consistency across reboots.
	   Here we calculate the index in same->iparray which
	   holds the ipaddress we should use */

	tmpip = ntohl(t->src.u3.ip);

	if (!(same->info & IPT_SAME_NODST))
		tmpip += ntohl(t->dst.u3.ip);
	aindex = tmpip % same->ipnum;

	new_ip = htonl(same->iparray[aindex]);

	DEBUGP("ipt_SAME: src=%u.%u.%u.%u dst=%u.%u.%u.%u, "
			"new src=%u.%u.%u.%u\n",
			NIPQUAD(t->src.ip), NIPQUAD(t->dst.ip),
			NIPQUAD(new_ip));

	/* Transfer from original range. */
	newrange = ((struct nf_nat_range)
		{ same->range[0].flags, new_ip, new_ip,
		  /* FIXME: Use ports from correct range! */
		  same->range[0].min, same->range[0].max });

	/* Hand modified range to generic setup. */
	return nf_nat_setup_info(ct, &newrange, hooknum);
}

static struct xt_target same_reg = {
	.name		= "SAME",
	.family		= AF_INET,
	.target		= same_target,
	.targetsize	= sizeof(struct ipt_same_info),
	.table		= "nat",
	.hooks		= (1 << NF_IP_PRE_ROUTING | 1 << NF_IP_POST_ROUTING),
	.checkentry	= same_check,
	.destroy	= same_destroy,
	.me		= THIS_MODULE,
};

static int __init ipt_same_init(void)
{
	return xt_register_target(&same_reg);
}

static void __exit ipt_same_fini(void)
{
	xt_unregister_target(&same_reg);
}

module_init(ipt_same_init);
module_exit(ipt_same_fini);

