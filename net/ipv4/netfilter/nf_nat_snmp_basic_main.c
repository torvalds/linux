/*
 * nf_nat_snmp_basic.c
 *
 * Basic SNMP Application Layer Gateway
 *
 * This IP NAT module is intended for use with SNMP network
 * discovery and monitoring applications where target networks use
 * conflicting private address realms.
 *
 * Static NAT is used to remap the networks from the view of the network
 * management system at the IP layer, and this module remaps some application
 * layer addresses to match.
 *
 * The simplest form of ALG is performed, where only tagged IP addresses
 * are modified.  The module does not need to be MIB aware and only scans
 * messages at the ASN.1/BER level.
 *
 * Currently, only SNMPv1 and SNMPv2 are supported.
 *
 * More information on ALG and associated issues can be found in
 * RFC 2962
 *
 * The ASB.1/BER parsing code is derived from the gxsnmp package by Gregory
 * McLean & Jochen Friedrich, stripped down for use in the kernel.
 *
 * Copyright (c) 2000 RP Internet (www.rpi.net.au).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: James Morris <jmorris@intercode.com.au>
 *
 * Copyright (c) 2006-2010 Patrick McHardy <kaber@trash.net>
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <net/checksum.h>
#include <net/udp.h>

#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_conntrack_expect.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <linux/netfilter/nf_conntrack_snmp.h>
#include "nf_nat_snmp_basic.asn1.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("James Morris <jmorris@intercode.com.au>");
MODULE_DESCRIPTION("Basic SNMP Application Layer Gateway");
MODULE_ALIAS("ip_nat_snmp_basic");
MODULE_ALIAS_NFCT_HELPER("snmp_trap");

#define SNMP_PORT 161
#define SNMP_TRAP_PORT 162

static DEFINE_SPINLOCK(snmp_lock);

struct snmp_ctx {
	unsigned char *begin;
	__sum16 *check;
	__be32 from;
	__be32 to;
};

static void fast_csum(struct snmp_ctx *ctx, unsigned char offset)
{
	unsigned char s[12] = {0,};
	int size;

	if (offset & 1) {
		memcpy(&s[1], &ctx->from, 4);
		memcpy(&s[7], &ctx->to, 4);
		s[0] = ~0;
		s[1] = ~s[1];
		s[2] = ~s[2];
		s[3] = ~s[3];
		s[4] = ~s[4];
		s[5] = ~0;
		size = 12;
	} else {
		memcpy(&s[0], &ctx->from, 4);
		memcpy(&s[4], &ctx->to, 4);
		s[0] = ~s[0];
		s[1] = ~s[1];
		s[2] = ~s[2];
		s[3] = ~s[3];
		size = 8;
	}
	*ctx->check = csum_fold(csum_partial(s, size,
					     ~csum_unfold(*ctx->check)));
}

int snmp_version(void *context, size_t hdrlen, unsigned char tag,
		 const void *data, size_t datalen)
{
	if (*(unsigned char *)data > 1)
		return -ENOTSUPP;
	return 1;
}

int snmp_helper(void *context, size_t hdrlen, unsigned char tag,
		const void *data, size_t datalen)
{
	struct snmp_ctx *ctx = (struct snmp_ctx *)context;
	__be32 *pdata = (__be32 *)data;

	if (*pdata == ctx->from) {
		pr_debug("%s: %pI4 to %pI4\n", __func__,
			 (void *)&ctx->from, (void *)&ctx->to);

		if (*ctx->check)
			fast_csum(ctx, (unsigned char *)data - ctx->begin);
		*pdata = ctx->to;
	}

	return 1;
}

static int snmp_translate(struct nf_conn *ct, int dir, struct sk_buff *skb)
{
	struct iphdr *iph = ip_hdr(skb);
	struct udphdr *udph = (struct udphdr *)((__be32 *)iph + iph->ihl);
	u16 datalen = ntohs(udph->len) - sizeof(struct udphdr);
	char *data = (unsigned char *)udph + sizeof(struct udphdr);
	struct snmp_ctx ctx;
	int ret;

	if (dir == IP_CT_DIR_ORIGINAL) {
		ctx.from = ct->tuplehash[dir].tuple.src.u3.ip;
		ctx.to = ct->tuplehash[!dir].tuple.dst.u3.ip;
	} else {
		ctx.from = ct->tuplehash[!dir].tuple.src.u3.ip;
		ctx.to = ct->tuplehash[dir].tuple.dst.u3.ip;
	}

	if (ctx.from == ctx.to)
		return NF_ACCEPT;

	ctx.begin = (unsigned char *)udph + sizeof(struct udphdr);
	ctx.check = &udph->check;
	ret = asn1_ber_decoder(&nf_nat_snmp_basic_decoder, &ctx, data, datalen);
	if (ret < 0) {
		nf_ct_helper_log(skb, ct, "parser failed\n");
		return NF_DROP;
	}

	return NF_ACCEPT;
}

/* We don't actually set up expectations, just adjust internal IP
 * addresses if this is being NATted
 */
static int help(struct sk_buff *skb, unsigned int protoff,
		struct nf_conn *ct,
		enum ip_conntrack_info ctinfo)
{
	int dir = CTINFO2DIR(ctinfo);
	unsigned int ret;
	const struct iphdr *iph = ip_hdr(skb);
	const struct udphdr *udph = (struct udphdr *)((__be32 *)iph + iph->ihl);

	/* SNMP replies and originating SNMP traps get mangled */
	if (udph->source == htons(SNMP_PORT) && dir != IP_CT_DIR_REPLY)
		return NF_ACCEPT;
	if (udph->dest == htons(SNMP_TRAP_PORT) && dir != IP_CT_DIR_ORIGINAL)
		return NF_ACCEPT;

	/* No NAT? */
	if (!(ct->status & IPS_NAT_MASK))
		return NF_ACCEPT;

	/* Make sure the packet length is ok.  So far, we were only guaranteed
	 * to have a valid length IP header plus 8 bytes, which means we have
	 * enough room for a UDP header.  Just verify the UDP length field so we
	 * can mess around with the payload.
	 */
	if (ntohs(udph->len) != skb->len - (iph->ihl << 2)) {
		nf_ct_helper_log(skb, ct, "dropping malformed packet\n");
		return NF_DROP;
	}

	if (!skb_make_writable(skb, skb->len)) {
		nf_ct_helper_log(skb, ct, "cannot mangle packet");
		return NF_DROP;
	}

	spin_lock_bh(&snmp_lock);
	ret = snmp_translate(ct, dir, skb);
	spin_unlock_bh(&snmp_lock);
	return ret;
}

static const struct nf_conntrack_expect_policy snmp_exp_policy = {
	.max_expected	= 0,
	.timeout	= 180,
};

static struct nf_conntrack_helper snmp_trap_helper __read_mostly = {
	.me			= THIS_MODULE,
	.help			= help,
	.expect_policy		= &snmp_exp_policy,
	.name			= "snmp_trap",
	.tuple.src.l3num	= AF_INET,
	.tuple.src.u.udp.port	= cpu_to_be16(SNMP_TRAP_PORT),
	.tuple.dst.protonum	= IPPROTO_UDP,
};

static int __init nf_nat_snmp_basic_init(void)
{
	BUG_ON(nf_nat_snmp_hook != NULL);
	RCU_INIT_POINTER(nf_nat_snmp_hook, help);

	return nf_conntrack_helper_register(&snmp_trap_helper);
}

static void __exit nf_nat_snmp_basic_fini(void)
{
	RCU_INIT_POINTER(nf_nat_snmp_hook, NULL);
	synchronize_rcu();
	nf_conntrack_helper_unregister(&snmp_trap_helper);
}

module_init(nf_nat_snmp_basic_init);
module_exit(nf_nat_snmp_basic_fini);
