/* Kernel module to match L2TP header parameters. */

/* (C) 2013      James Chapman <jchapman@katalix.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/if_ether.h>
#include <net/ip.h>
#include <linux/ipv6.h>
#include <net/ipv6.h>
#include <net/udp.h>
#include <linux/l2tp.h>

#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_tcpudp.h>
#include <linux/netfilter/xt_l2tp.h>

/* L2TP header masks */
#define L2TP_HDR_T_BIT	0x8000
#define L2TP_HDR_L_BIT	0x4000
#define L2TP_HDR_VER	0x000f

MODULE_LICENSE("GPL");
MODULE_AUTHOR("James Chapman <jchapman@katalix.com>");
MODULE_DESCRIPTION("Xtables: L2TP header match");
MODULE_ALIAS("ipt_l2tp");
MODULE_ALIAS("ip6t_l2tp");

/* The L2TP fields that can be matched */
struct l2tp_data {
	u32 tid;
	u32 sid;
	u8 type;
	u8 version;
};

union l2tp_val {
	__be16 val16[2];
	__be32 val32;
};

static bool l2tp_match(const struct xt_l2tp_info *info, struct l2tp_data *data)
{
	if ((info->flags & XT_L2TP_TYPE) && (info->type != data->type))
		return false;

	if ((info->flags & XT_L2TP_VERSION) && (info->version != data->version))
		return false;

	/* Check tid only for L2TPv3 control or any L2TPv2 packets */
	if ((info->flags & XT_L2TP_TID) &&
	    ((data->type == XT_L2TP_TYPE_CONTROL) || (data->version == 2)) &&
	    (info->tid != data->tid))
		return false;

	/* Check sid only for L2TP data packets */
	if ((info->flags & XT_L2TP_SID) && (data->type == XT_L2TP_TYPE_DATA) &&
	    (info->sid != data->sid))
		return false;

	return true;
}

/* Parse L2TP header fields when UDP encapsulation is used. Handles
 * L2TPv2 and L2TPv3. Note the L2TPv3 control and data packets have a
 * different format. See
 * RFC2661, Section 3.1, L2TPv2 Header Format
 * RFC3931, Section 3.2.1, L2TPv3 Control Message Header
 * RFC3931, Section 3.2.2, L2TPv3 Data Message Header
 * RFC3931, Section 4.1.2.1, L2TPv3 Session Header over UDP
 */
static bool l2tp_udp_mt(const struct sk_buff *skb, struct xt_action_param *par, u16 thoff)
{
	const struct xt_l2tp_info *info = par->matchinfo;
	int uhlen = sizeof(struct udphdr);
	int offs = thoff + uhlen;
	union l2tp_val *lh;
	union l2tp_val lhbuf;
	u16 flags;
	struct l2tp_data data = { 0, };

	if (par->fragoff != 0)
		return false;

	/* Extract L2TP header fields. The flags in the first 16 bits
	 * tell us where the other fields are.
	 */
	lh = skb_header_pointer(skb, offs, 2, &lhbuf);
	if (lh == NULL)
		return false;

	flags = ntohs(lh->val16[0]);
	if (flags & L2TP_HDR_T_BIT)
		data.type = XT_L2TP_TYPE_CONTROL;
	else
		data.type = XT_L2TP_TYPE_DATA;
	data.version = (u8) flags & L2TP_HDR_VER;

	/* Now extract the L2TP tid/sid. These are in different places
	 * for L2TPv2 (rfc2661) and L2TPv3 (rfc3931). For L2TPv2, we
	 * must also check to see if the length field is present,
	 * since this affects the offsets into the packet of the
	 * tid/sid fields.
	 */
	if (data.version == 3) {
		lh = skb_header_pointer(skb, offs + 4, 4, &lhbuf);
		if (lh == NULL)
			return false;
		if (data.type == XT_L2TP_TYPE_CONTROL)
			data.tid = ntohl(lh->val32);
		else
			data.sid = ntohl(lh->val32);
	} else if (data.version == 2) {
		if (flags & L2TP_HDR_L_BIT)
			offs += 2;
		lh = skb_header_pointer(skb, offs + 2, 4, &lhbuf);
		if (lh == NULL)
			return false;
		data.tid = (u32) ntohs(lh->val16[0]);
		data.sid = (u32) ntohs(lh->val16[1]);
	} else
		return false;

	return l2tp_match(info, &data);
}

/* Parse L2TP header fields for IP encapsulation (no UDP header).
 * L2TPv3 data packets have a different form with IP encap. See
 * RC3931, Section 4.1.1.1, L2TPv3 Session Header over IP.
 * RC3931, Section 4.1.1.2, L2TPv3 Control and Data Traffic over IP.
 */
static bool l2tp_ip_mt(const struct sk_buff *skb, struct xt_action_param *par, u16 thoff)
{
	const struct xt_l2tp_info *info = par->matchinfo;
	union l2tp_val *lh;
	union l2tp_val lhbuf;
	struct l2tp_data data = { 0, };

	/* For IP encap, the L2TP sid is the first 32-bits. */
	lh = skb_header_pointer(skb, thoff, sizeof(lhbuf), &lhbuf);
	if (lh == NULL)
		return false;
	if (lh->val32 == 0) {
		/* Must be a control packet. The L2TP tid is further
		 * into the packet.
		 */
		data.type = XT_L2TP_TYPE_CONTROL;
		lh = skb_header_pointer(skb, thoff + 8, sizeof(lhbuf),
					&lhbuf);
		if (lh == NULL)
			return false;
		data.tid = ntohl(lh->val32);
	} else {
		data.sid = ntohl(lh->val32);
		data.type = XT_L2TP_TYPE_DATA;
	}

	data.version = 3;

	return l2tp_match(info, &data);
}

static bool l2tp_mt4(const struct sk_buff *skb, struct xt_action_param *par)
{
	struct iphdr *iph = ip_hdr(skb);
	u8 ipproto = iph->protocol;

	/* l2tp_mt_check4 already restricts the transport protocol */
	switch (ipproto) {
	case IPPROTO_UDP:
		return l2tp_udp_mt(skb, par, par->thoff);
	case IPPROTO_L2TP:
		return l2tp_ip_mt(skb, par, par->thoff);
	}

	return false;
}

#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
static bool l2tp_mt6(const struct sk_buff *skb, struct xt_action_param *par)
{
	unsigned int thoff = 0;
	unsigned short fragoff = 0;
	int ipproto;

	ipproto = ipv6_find_hdr(skb, &thoff, -1, &fragoff, NULL);
	if (fragoff != 0)
		return false;

	/* l2tp_mt_check6 already restricts the transport protocol */
	switch (ipproto) {
	case IPPROTO_UDP:
		return l2tp_udp_mt(skb, par, thoff);
	case IPPROTO_L2TP:
		return l2tp_ip_mt(skb, par, thoff);
	}

	return false;
}
#endif

static int l2tp_mt_check(const struct xt_mtchk_param *par)
{
	const struct xt_l2tp_info *info = par->matchinfo;

	/* Check for invalid flags */
	if (info->flags & ~(XT_L2TP_TID | XT_L2TP_SID | XT_L2TP_VERSION |
			    XT_L2TP_TYPE)) {
		pr_info("unknown flags: %x\n", info->flags);
		return -EINVAL;
	}

	/* At least one of tid, sid or type=control must be specified */
	if ((!(info->flags & XT_L2TP_TID)) &&
	    (!(info->flags & XT_L2TP_SID)) &&
	    ((!(info->flags & XT_L2TP_TYPE)) ||
	     (info->type != XT_L2TP_TYPE_CONTROL))) {
		pr_info("invalid flags combination: %x\n", info->flags);
		return -EINVAL;
	}

	/* If version 2 is specified, check that incompatible params
	 * are not supplied
	 */
	if (info->flags & XT_L2TP_VERSION) {
		if ((info->version < 2) || (info->version > 3)) {
			pr_info("wrong L2TP version: %u\n", info->version);
			return -EINVAL;
		}

		if (info->version == 2) {
			if ((info->flags & XT_L2TP_TID) &&
			    (info->tid > 0xffff)) {
				pr_info("v2 tid > 0xffff: %u\n", info->tid);
				return -EINVAL;
			}
			if ((info->flags & XT_L2TP_SID) &&
			    (info->sid > 0xffff)) {
				pr_info("v2 sid > 0xffff: %u\n", info->sid);
				return -EINVAL;
			}
		}
	}

	return 0;
}

static int l2tp_mt_check4(const struct xt_mtchk_param *par)
{
	const struct xt_l2tp_info *info = par->matchinfo;
	const struct ipt_entry *e = par->entryinfo;
	const struct ipt_ip *ip = &e->ip;
	int ret;

	ret = l2tp_mt_check(par);
	if (ret != 0)
		return ret;

	if ((ip->proto != IPPROTO_UDP) &&
	    (ip->proto != IPPROTO_L2TP)) {
		pr_info("missing protocol rule (udp|l2tpip)\n");
		return -EINVAL;
	}

	if ((ip->proto == IPPROTO_L2TP) &&
	    (info->version == 2)) {
		pr_info("v2 doesn't support IP mode\n");
		return -EINVAL;
	}

	return 0;
}

#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
static int l2tp_mt_check6(const struct xt_mtchk_param *par)
{
	const struct xt_l2tp_info *info = par->matchinfo;
	const struct ip6t_entry *e = par->entryinfo;
	const struct ip6t_ip6 *ip = &e->ipv6;
	int ret;

	ret = l2tp_mt_check(par);
	if (ret != 0)
		return ret;

	if ((ip->proto != IPPROTO_UDP) &&
	    (ip->proto != IPPROTO_L2TP)) {
		pr_info("missing protocol rule (udp|l2tpip)\n");
		return -EINVAL;
	}

	if ((ip->proto == IPPROTO_L2TP) &&
	    (info->version == 2)) {
		pr_info("v2 doesn't support IP mode\n");
		return -EINVAL;
	}

	return 0;
}
#endif

static struct xt_match l2tp_mt_reg[] __read_mostly = {
	{
		.name      = "l2tp",
		.revision  = 0,
		.family    = NFPROTO_IPV4,
		.match     = l2tp_mt4,
		.matchsize = XT_ALIGN(sizeof(struct xt_l2tp_info)),
		.checkentry = l2tp_mt_check4,
		.hooks     = ((1 << NF_INET_PRE_ROUTING) |
			      (1 << NF_INET_LOCAL_IN) |
			      (1 << NF_INET_LOCAL_OUT) |
			      (1 << NF_INET_FORWARD)),
		.me        = THIS_MODULE,
	},
#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
	{
		.name      = "l2tp",
		.revision  = 0,
		.family    = NFPROTO_IPV6,
		.match     = l2tp_mt6,
		.matchsize = XT_ALIGN(sizeof(struct xt_l2tp_info)),
		.checkentry = l2tp_mt_check6,
		.hooks     = ((1 << NF_INET_PRE_ROUTING) |
			      (1 << NF_INET_LOCAL_IN) |
			      (1 << NF_INET_LOCAL_OUT) |
			      (1 << NF_INET_FORWARD)),
		.me        = THIS_MODULE,
	},
#endif
};

static int __init l2tp_mt_init(void)
{
	return xt_register_matches(&l2tp_mt_reg[0], ARRAY_SIZE(l2tp_mt_reg));
}

static void __exit l2tp_mt_exit(void)
{
	xt_unregister_matches(&l2tp_mt_reg[0], ARRAY_SIZE(l2tp_mt_reg));
}

module_init(l2tp_mt_init);
module_exit(l2tp_mt_exit);
