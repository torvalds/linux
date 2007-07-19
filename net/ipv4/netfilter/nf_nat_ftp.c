/* FTP extension for TCP NAT alteration. */

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2006 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/netfilter_ipv4.h>
#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_nat_helper.h>
#include <net/netfilter/nf_nat_rule.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_expect.h>
#include <linux/netfilter/nf_conntrack_ftp.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Rusty Russell <rusty@rustcorp.com.au>");
MODULE_DESCRIPTION("ftp NAT helper");
MODULE_ALIAS("ip_nat_ftp");

/* FIXME: Time out? --RR */

static int
mangle_rfc959_packet(struct sk_buff **pskb,
		     __be32 newip,
		     u_int16_t port,
		     unsigned int matchoff,
		     unsigned int matchlen,
		     struct nf_conn *ct,
		     enum ip_conntrack_info ctinfo)
{
	char buffer[sizeof("nnn,nnn,nnn,nnn,nnn,nnn")];

	sprintf(buffer, "%u,%u,%u,%u,%u,%u",
		NIPQUAD(newip), port>>8, port&0xFF);

	pr_debug("calling nf_nat_mangle_tcp_packet\n");

	return nf_nat_mangle_tcp_packet(pskb, ct, ctinfo, matchoff,
					matchlen, buffer, strlen(buffer));
}

/* |1|132.235.1.2|6275| */
static int
mangle_eprt_packet(struct sk_buff **pskb,
		   __be32 newip,
		   u_int16_t port,
		   unsigned int matchoff,
		   unsigned int matchlen,
		   struct nf_conn *ct,
		   enum ip_conntrack_info ctinfo)
{
	char buffer[sizeof("|1|255.255.255.255|65535|")];

	sprintf(buffer, "|1|%u.%u.%u.%u|%u|", NIPQUAD(newip), port);

	pr_debug("calling nf_nat_mangle_tcp_packet\n");

	return nf_nat_mangle_tcp_packet(pskb, ct, ctinfo, matchoff,
					matchlen, buffer, strlen(buffer));
}

/* |1|132.235.1.2|6275| */
static int
mangle_epsv_packet(struct sk_buff **pskb,
		   __be32 newip,
		   u_int16_t port,
		   unsigned int matchoff,
		   unsigned int matchlen,
		   struct nf_conn *ct,
		   enum ip_conntrack_info ctinfo)
{
	char buffer[sizeof("|||65535|")];

	sprintf(buffer, "|||%u|", port);

	pr_debug("calling nf_nat_mangle_tcp_packet\n");

	return nf_nat_mangle_tcp_packet(pskb, ct, ctinfo, matchoff,
					matchlen, buffer, strlen(buffer));
}

static int (*mangle[])(struct sk_buff **, __be32, u_int16_t,
		       unsigned int, unsigned int, struct nf_conn *,
		       enum ip_conntrack_info)
= {
	[NF_CT_FTP_PORT] = mangle_rfc959_packet,
	[NF_CT_FTP_PASV] = mangle_rfc959_packet,
	[NF_CT_FTP_EPRT] = mangle_eprt_packet,
	[NF_CT_FTP_EPSV] = mangle_epsv_packet
};

/* So, this packet has hit the connection tracking matching code.
   Mangle it, and change the expectation to match the new version. */
static unsigned int nf_nat_ftp(struct sk_buff **pskb,
			       enum ip_conntrack_info ctinfo,
			       enum nf_ct_ftp_type type,
			       unsigned int matchoff,
			       unsigned int matchlen,
			       struct nf_conntrack_expect *exp)
{
	__be32 newip;
	u_int16_t port;
	int dir = CTINFO2DIR(ctinfo);
	struct nf_conn *ct = exp->master;

	pr_debug("FTP_NAT: type %i, off %u len %u\n", type, matchoff, matchlen);

	/* Connection will come from wherever this packet goes, hence !dir */
	newip = ct->tuplehash[!dir].tuple.dst.u3.ip;
	exp->saved_proto.tcp.port = exp->tuple.dst.u.tcp.port;
	exp->dir = !dir;

	/* When you see the packet, we need to NAT it the same as the
	 * this one. */
	exp->expectfn = nf_nat_follow_master;

	/* Try to get same port: if not, try to change it. */
	for (port = ntohs(exp->saved_proto.tcp.port); port != 0; port++) {
		exp->tuple.dst.u.tcp.port = htons(port);
		if (nf_ct_expect_related(exp) == 0)
			break;
	}

	if (port == 0)
		return NF_DROP;

	if (!mangle[type](pskb, newip, port, matchoff, matchlen, ct, ctinfo)) {
		nf_ct_unexpect_related(exp);
		return NF_DROP;
	}
	return NF_ACCEPT;
}

static void __exit nf_nat_ftp_fini(void)
{
	rcu_assign_pointer(nf_nat_ftp_hook, NULL);
	synchronize_rcu();
}

static int __init nf_nat_ftp_init(void)
{
	BUG_ON(rcu_dereference(nf_nat_ftp_hook));
	rcu_assign_pointer(nf_nat_ftp_hook, nf_nat_ftp);
	return 0;
}

/* Prior to 2.6.11, we had a ports param.  No longer, but don't break users. */
static int warn_set(const char *val, struct kernel_param *kp)
{
	printk(KERN_INFO KBUILD_MODNAME
	       ": kernel >= 2.6.10 only uses 'ports' for conntrack modules\n");
	return 0;
}
module_param_call(ports, warn_set, NULL, NULL, 0);

module_init(nf_nat_ftp_init);
module_exit(nf_nat_ftp_fini);
