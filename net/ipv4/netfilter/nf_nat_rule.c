/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2006 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* Everything about the rules for NAT. */
#include <linux/types.h>
#include <linux/ip.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/skbuff.h>
#include <linux/proc_fs.h>
#include <net/checksum.h>
#include <net/route.h>
#include <linux/bitops.h>

#include <linux/netfilter_ipv4/ip_tables.h>
#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_nat_core.h>
#include <net/netfilter/nf_nat_rule.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

#define NAT_VALID_HOOKS ((1<<NF_IP_PRE_ROUTING) | (1<<NF_IP_POST_ROUTING) | (1<<NF_IP_LOCAL_OUT))

static struct
{
	struct ipt_replace repl;
	struct ipt_standard entries[3];
	struct ipt_error term;
} nat_initial_table __initdata = {
	.repl = {
		.name = "nat",
		.valid_hooks = NAT_VALID_HOOKS,
		.num_entries = 4,
		.size = sizeof(struct ipt_standard) * 3 + sizeof(struct ipt_error),
		.hook_entry = {
			[NF_IP_PRE_ROUTING] = 0,
			[NF_IP_POST_ROUTING] = sizeof(struct ipt_standard),
			[NF_IP_LOCAL_OUT] = sizeof(struct ipt_standard) * 2 },
		.underflow = {
			[NF_IP_PRE_ROUTING] = 0,
			[NF_IP_POST_ROUTING] = sizeof(struct ipt_standard),
			[NF_IP_LOCAL_OUT] = sizeof(struct ipt_standard) * 2 },
	},
	.entries = {
		/* PRE_ROUTING */
		{
			.entry = {
	    			.target_offset = sizeof(struct ipt_entry),
	    			.next_offset = sizeof(struct ipt_standard),
			},
			.target = {
				.target = {
					.u = {
						.target_size = IPT_ALIGN(sizeof(struct ipt_standard_target)),
					},
				},
				.verdict = -NF_ACCEPT - 1,
			},
		},
		/* POST_ROUTING */
		{
			.entry = {
	    			.target_offset = sizeof(struct ipt_entry),
	    			.next_offset = sizeof(struct ipt_standard),
			},
			.target = {
				.target = {
					.u = {
						.target_size = IPT_ALIGN(sizeof(struct ipt_standard_target)),
					},
				},
				.verdict = -NF_ACCEPT - 1,
			},
		},
		/* LOCAL_OUT */
		{
			.entry = {
	    			.target_offset = sizeof(struct ipt_entry),
	    			.next_offset = sizeof(struct ipt_standard),
			},
			.target = {
				.target = {
					.u = {
						.target_size = IPT_ALIGN(sizeof(struct ipt_standard_target)),
					},
				},
				.verdict = -NF_ACCEPT - 1,
			},
		},
	},
	/* ERROR */
	.term = {
		.entry = {
			.target_offset = sizeof(struct ipt_entry),
			.next_offset = sizeof(struct ipt_error),
		},
		.target = {
			.target = {
				.u = {
					.user = {
						.target_size = IPT_ALIGN(sizeof(struct ipt_error_target)),
						.name = IPT_ERROR_TARGET,
					},
				},
			},
			.errorname = "ERROR",
		},
	}
};

static struct ipt_table nat_table = {
	.name		= "nat",
	.valid_hooks	= NAT_VALID_HOOKS,
	.lock		= RW_LOCK_UNLOCKED,
	.me		= THIS_MODULE,
	.af		= AF_INET,
};

/* Source NAT */
static unsigned int ipt_snat_target(struct sk_buff **pskb,
				    const struct net_device *in,
				    const struct net_device *out,
				    unsigned int hooknum,
				    const struct xt_target *target,
				    const void *targinfo)
{
	struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	const struct nf_nat_multi_range_compat *mr = targinfo;

	NF_CT_ASSERT(hooknum == NF_IP_POST_ROUTING);

	ct = nf_ct_get(*pskb, &ctinfo);

	/* Connection must be valid and new. */
	NF_CT_ASSERT(ct && (ctinfo == IP_CT_NEW || ctinfo == IP_CT_RELATED ||
	                    ctinfo == IP_CT_RELATED + IP_CT_IS_REPLY));
	NF_CT_ASSERT(out);

	return nf_nat_setup_info(ct, &mr->range[0], hooknum);
}

/* Before 2.6.11 we did implicit source NAT if required. Warn about change. */
static void warn_if_extra_mangle(__be32 dstip, __be32 srcip)
{
	static int warned = 0;
	struct flowi fl = { .nl_u = { .ip4_u = { .daddr = dstip } } };
	struct rtable *rt;

	if (ip_route_output_key(&rt, &fl) != 0)
		return;

	if (rt->rt_src != srcip && !warned) {
		printk("NAT: no longer support implicit source local NAT\n");
		printk("NAT: packet src %u.%u.%u.%u -> dst %u.%u.%u.%u\n",
		       NIPQUAD(srcip), NIPQUAD(dstip));
		warned = 1;
	}
	ip_rt_put(rt);
}

static unsigned int ipt_dnat_target(struct sk_buff **pskb,
				    const struct net_device *in,
				    const struct net_device *out,
				    unsigned int hooknum,
				    const struct xt_target *target,
				    const void *targinfo)
{
	struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	const struct nf_nat_multi_range_compat *mr = targinfo;

	NF_CT_ASSERT(hooknum == NF_IP_PRE_ROUTING ||
		     hooknum == NF_IP_LOCAL_OUT);

	ct = nf_ct_get(*pskb, &ctinfo);

	/* Connection must be valid and new. */
	NF_CT_ASSERT(ct && (ctinfo == IP_CT_NEW || ctinfo == IP_CT_RELATED));

	if (hooknum == NF_IP_LOCAL_OUT &&
	    mr->range[0].flags & IP_NAT_RANGE_MAP_IPS)
		warn_if_extra_mangle((*pskb)->nh.iph->daddr,
				     mr->range[0].min_ip);

	return nf_nat_setup_info(ct, &mr->range[0], hooknum);
}

static int ipt_snat_checkentry(const char *tablename,
			       const void *entry,
			       const struct xt_target *target,
			       void *targinfo,
			       unsigned int hook_mask)
{
	struct nf_nat_multi_range_compat *mr = targinfo;

	/* Must be a valid range */
	if (mr->rangesize != 1) {
		printk("SNAT: multiple ranges no longer supported\n");
		return 0;
	}
	return 1;
}

static int ipt_dnat_checkentry(const char *tablename,
			       const void *entry,
			       const struct xt_target *target,
			       void *targinfo,
			       unsigned int hook_mask)
{
	struct nf_nat_multi_range_compat *mr = targinfo;

	/* Must be a valid range */
	if (mr->rangesize != 1) {
		printk("DNAT: multiple ranges no longer supported\n");
		return 0;
	}
	return 1;
}

inline unsigned int
alloc_null_binding(struct nf_conn *ct,
		   struct nf_nat_info *info,
		   unsigned int hooknum)
{
	/* Force range to this IP; let proto decide mapping for
	   per-proto parts (hence not IP_NAT_RANGE_PROTO_SPECIFIED).
	   Use reply in case it's already been mangled (eg local packet).
	*/
	__be32 ip
		= (HOOK2MANIP(hooknum) == IP_NAT_MANIP_SRC
		   ? ct->tuplehash[IP_CT_DIR_REPLY].tuple.dst.u3.ip
		   : ct->tuplehash[IP_CT_DIR_REPLY].tuple.src.u3.ip);
	struct nf_nat_range range
		= { IP_NAT_RANGE_MAP_IPS, ip, ip, { 0 }, { 0 } };

	DEBUGP("Allocating NULL binding for %p (%u.%u.%u.%u)\n",
	       ct, NIPQUAD(ip));
	return nf_nat_setup_info(ct, &range, hooknum);
}

unsigned int
alloc_null_binding_confirmed(struct nf_conn *ct,
                             struct nf_nat_info *info,
                             unsigned int hooknum)
{
	__be32 ip
		= (HOOK2MANIP(hooknum) == IP_NAT_MANIP_SRC
		   ? ct->tuplehash[IP_CT_DIR_REPLY].tuple.dst.u3.ip
		   : ct->tuplehash[IP_CT_DIR_REPLY].tuple.src.u3.ip);
	u_int16_t all
		= (HOOK2MANIP(hooknum) == IP_NAT_MANIP_SRC
		   ? ct->tuplehash[IP_CT_DIR_REPLY].tuple.dst.u.all
		   : ct->tuplehash[IP_CT_DIR_REPLY].tuple.src.u.all);
	struct nf_nat_range range
		= { IP_NAT_RANGE_MAP_IPS, ip, ip, { all }, { all } };

	DEBUGP("Allocating NULL binding for confirmed %p (%u.%u.%u.%u)\n",
	       ct, NIPQUAD(ip));
	return nf_nat_setup_info(ct, &range, hooknum);
}

int nf_nat_rule_find(struct sk_buff **pskb,
		     unsigned int hooknum,
		     const struct net_device *in,
		     const struct net_device *out,
		     struct nf_conn *ct,
		     struct nf_nat_info *info)
{
	int ret;

	ret = ipt_do_table(pskb, hooknum, in, out, &nat_table);

	if (ret == NF_ACCEPT) {
		if (!nf_nat_initialized(ct, HOOK2MANIP(hooknum)))
			/* NUL mapping */
			ret = alloc_null_binding(ct, info, hooknum);
	}
	return ret;
}

static struct ipt_target ipt_snat_reg = {
	.name		= "SNAT",
	.target		= ipt_snat_target,
	.targetsize	= sizeof(struct nf_nat_multi_range_compat),
	.table		= "nat",
	.hooks		= 1 << NF_IP_POST_ROUTING,
	.checkentry	= ipt_snat_checkentry,
	.family		= AF_INET,
};

static struct xt_target ipt_dnat_reg = {
	.name		= "DNAT",
	.target		= ipt_dnat_target,
	.targetsize	= sizeof(struct nf_nat_multi_range_compat),
	.table		= "nat",
	.hooks		= (1 << NF_IP_PRE_ROUTING) | (1 << NF_IP_LOCAL_OUT),
	.checkentry	= ipt_dnat_checkentry,
	.family		= AF_INET,
};

int __init nf_nat_rule_init(void)
{
	int ret;

	ret = ipt_register_table(&nat_table, &nat_initial_table.repl);
	if (ret != 0)
		return ret;
	ret = xt_register_target(&ipt_snat_reg);
	if (ret != 0)
		goto unregister_table;

	ret = xt_register_target(&ipt_dnat_reg);
	if (ret != 0)
		goto unregister_snat;

	return ret;

 unregister_snat:
	xt_unregister_target(&ipt_snat_reg);
 unregister_table:
	ipt_unregister_table(&nat_table);

	return ret;
}

void nf_nat_rule_cleanup(void)
{
	xt_unregister_target(&ipt_dnat_reg);
	xt_unregister_target(&ipt_snat_reg);
	ipt_unregister_table(&nat_table);
}
