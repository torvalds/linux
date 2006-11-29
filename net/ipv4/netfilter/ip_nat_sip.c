/* SIP extension for UDP NAT alteration.
 *
 * (C) 2005 by Christian Hentschel <chentschel@arnet.com.ar>
 * based on RR's ip_nat_ftp.c and other modules.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/udp.h>

#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv4/ip_nat.h>
#include <linux/netfilter_ipv4/ip_nat_helper.h>
#include <linux/netfilter_ipv4/ip_conntrack_helper.h>
#include <linux/netfilter_ipv4/ip_conntrack_sip.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian Hentschel <chentschel@arnet.com.ar>");
MODULE_DESCRIPTION("SIP NAT helper");

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

extern struct sip_header_nfo ct_sip_hdrs[];

static unsigned int mangle_sip_packet(struct sk_buff **pskb,
				      enum ip_conntrack_info ctinfo,
				      struct ip_conntrack *ct,
				      const char **dptr, size_t dlen,
				      char *buffer, int bufflen,
				      struct sip_header_nfo *hnfo)
{
	unsigned int matchlen, matchoff;

	if (ct_sip_get_info(*dptr, dlen, &matchoff, &matchlen, hnfo) <= 0)
		return 0;

	if (!ip_nat_mangle_udp_packet(pskb, ct, ctinfo,
	                              matchoff, matchlen, buffer, bufflen))
		return 0;

	/* We need to reload this. Thanks Patrick. */
	*dptr = (*pskb)->data + (*pskb)->nh.iph->ihl*4 + sizeof(struct udphdr);
	return 1;
}

static unsigned int ip_nat_sip(struct sk_buff **pskb,
			       enum ip_conntrack_info ctinfo,
			       struct ip_conntrack *ct,
			       const char **dptr)
{
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	char buffer[sizeof("nnn.nnn.nnn.nnn:nnnnn")];
	unsigned int bufflen, dataoff;
	__be32 ip;
	__be16 port;

	dataoff = (*pskb)->nh.iph->ihl*4 + sizeof(struct udphdr);

	ip   = ct->tuplehash[!dir].tuple.dst.ip;
	port = ct->tuplehash[!dir].tuple.dst.u.udp.port;
	bufflen = sprintf(buffer, "%u.%u.%u.%u:%u", NIPQUAD(ip), ntohs(port));

	/* short packet ? */
	if (((*pskb)->len - dataoff) < (sizeof("SIP/2.0") - 1))
		return 0;

	/* Basic rules: requests and responses. */
	if (memcmp(*dptr, "SIP/2.0", sizeof("SIP/2.0") - 1) == 0) {
		const char *aux;

		if ((ctinfo) < IP_CT_IS_REPLY) {
			mangle_sip_packet(pskb, ctinfo, ct, dptr,
			                  (*pskb)->len - dataoff,
			                  buffer, bufflen,
			                  &ct_sip_hdrs[POS_CONTACT]);
			return 1;
		}

		if (!mangle_sip_packet(pskb, ctinfo, ct, dptr,
				       (*pskb)->len - dataoff,
		                       buffer, bufflen, &ct_sip_hdrs[POS_VIA]))
			return 0;

		/* This search should ignore case, but later.. */
		aux = ct_sip_search("CSeq:", *dptr, sizeof("CSeq:") - 1,
		                    (*pskb)->len - dataoff);
		if (!aux)
			return 0;

		if (!ct_sip_search("REGISTER", aux, sizeof("REGISTER"),
		    ct_sip_lnlen(aux, *dptr + (*pskb)->len - dataoff)))
			return 1;

		return mangle_sip_packet(pskb, ctinfo, ct, dptr,
					 (*pskb)->len - dataoff,
		                         buffer, bufflen,
					 &ct_sip_hdrs[POS_CONTACT]);
	}
	if ((ctinfo) < IP_CT_IS_REPLY) {
		if (!mangle_sip_packet(pskb, ctinfo, ct, dptr,
				       (*pskb)->len - dataoff,
		                       buffer, bufflen, &ct_sip_hdrs[POS_VIA]))
			return 0;

		/* Mangle Contact if exists only. - watch udp_nat_mangle()! */
		mangle_sip_packet(pskb, ctinfo, ct, dptr, (*pskb)->len - dataoff,
		                  buffer, bufflen, &ct_sip_hdrs[POS_CONTACT]);
		return 1;
	}
	/* This mangle requests headers. */
	return mangle_sip_packet(pskb, ctinfo, ct, dptr,
	                         ct_sip_lnlen(*dptr,
				              *dptr + (*pskb)->len - dataoff),
	                         buffer, bufflen, &ct_sip_hdrs[POS_REQ_HEADER]);
}

static int mangle_content_len(struct sk_buff **pskb,
			      enum ip_conntrack_info ctinfo,
			      struct ip_conntrack *ct,
			      const char *dptr)
{
	unsigned int dataoff, matchoff, matchlen;
	char buffer[sizeof("65536")];
	int bufflen;

	dataoff = (*pskb)->nh.iph->ihl*4 + sizeof(struct udphdr);

	/* Get actual SDP lenght */
	if (ct_sip_get_info(dptr, (*pskb)->len - dataoff, &matchoff,
	                    &matchlen, &ct_sip_hdrs[POS_SDP_HEADER]) > 0) {

		/* since ct_sip_get_info() give us a pointer passing 'v='
		   we need to add 2 bytes in this count. */
		int c_len = (*pskb)->len - dataoff - matchoff + 2;

		/* Now, update SDP lenght */
		if (ct_sip_get_info(dptr, (*pskb)->len - dataoff, &matchoff,
		                    &matchlen, &ct_sip_hdrs[POS_CONTENT]) > 0) {

			bufflen = sprintf(buffer, "%u", c_len);

			return ip_nat_mangle_udp_packet(pskb, ct, ctinfo,
							matchoff, matchlen,
							buffer, bufflen);
		}
	}
	return 0;
}

static unsigned int mangle_sdp(struct sk_buff **pskb,
			       enum ip_conntrack_info ctinfo,
			       struct ip_conntrack *ct,
			       __be32 newip, u_int16_t port,
			       const char *dptr)
{
	char buffer[sizeof("nnn.nnn.nnn.nnn")];
	unsigned int dataoff, bufflen;

	dataoff = (*pskb)->nh.iph->ihl*4 + sizeof(struct udphdr);

	/* Mangle owner and contact info. */
	bufflen = sprintf(buffer, "%u.%u.%u.%u", NIPQUAD(newip));
	if (!mangle_sip_packet(pskb, ctinfo, ct, &dptr, (*pskb)->len - dataoff,
	                       buffer, bufflen, &ct_sip_hdrs[POS_OWNER]))
		return 0;

	if (!mangle_sip_packet(pskb, ctinfo, ct, &dptr, (*pskb)->len - dataoff,
	                       buffer, bufflen, &ct_sip_hdrs[POS_CONNECTION]))
		return 0;

	/* Mangle media port. */
	bufflen = sprintf(buffer, "%u", port);
	if (!mangle_sip_packet(pskb, ctinfo, ct, &dptr, (*pskb)->len - dataoff,
	                       buffer, bufflen, &ct_sip_hdrs[POS_MEDIA]))
		return 0;

	return mangle_content_len(pskb, ctinfo, ct, dptr);
}

/* So, this packet has hit the connection tracking matching code.
   Mangle it, and change the expectation to match the new version. */
static unsigned int ip_nat_sdp(struct sk_buff **pskb,
			       enum ip_conntrack_info ctinfo,
			       struct ip_conntrack_expect *exp,
			       const char *dptr)
{
	struct ip_conntrack *ct = exp->master;
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	__be32 newip;
	u_int16_t port;

	DEBUGP("ip_nat_sdp():\n");

	/* Connection will come from reply */
	newip = ct->tuplehash[!dir].tuple.dst.ip;

	exp->tuple.dst.ip = newip;
	exp->saved_proto.udp.port = exp->tuple.dst.u.udp.port;
	exp->dir = !dir;

	/* When you see the packet, we need to NAT it the same as the
	   this one. */
	exp->expectfn = ip_nat_follow_master;

	/* Try to get same port: if not, try to change it. */
	for (port = ntohs(exp->saved_proto.udp.port); port != 0; port++) {
		exp->tuple.dst.u.udp.port = htons(port);
		if (ip_conntrack_expect_related(exp) == 0)
			break;
	}

	if (port == 0)
		return NF_DROP;

	if (!mangle_sdp(pskb, ctinfo, ct, newip, port, dptr)) {
		ip_conntrack_unexpect_related(exp);
		return NF_DROP;
	}
	return NF_ACCEPT;
}

static void __exit fini(void)
{
	rcu_assign_pointer(ip_nat_sip_hook, NULL);
	rcu_assign_pointer(ip_nat_sdp_hook, NULL);
	synchronize_rcu();
}

static int __init init(void)
{
	BUG_ON(rcu_dereference(ip_nat_sip_hook));
	BUG_ON(rcu_dereference(ip_nat_sdp_hook));
	rcu_assign_pointer(ip_nat_sip_hook, ip_nat_sip);
	rcu_assign_pointer(ip_nat_sdp_hook, ip_nat_sdp);
	return 0;
}

module_init(init);
module_exit(fini);
