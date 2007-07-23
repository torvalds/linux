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
#include <net/ip.h>
#include <linux/udp.h>

#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_nat_helper.h>
#include <net/netfilter/nf_nat_rule.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_expect.h>
#include <linux/netfilter/nf_conntrack_sip.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian Hentschel <chentschel@arnet.com.ar>");
MODULE_DESCRIPTION("SIP NAT helper");
MODULE_ALIAS("ip_nat_sip");

struct addr_map {
	struct {
		char		src[sizeof("nnn.nnn.nnn.nnn:nnnnn")];
		char		dst[sizeof("nnn.nnn.nnn.nnn:nnnnn")];
		unsigned int	srclen, srciplen;
		unsigned int	dstlen, dstiplen;
	} addr[IP_CT_DIR_MAX];
};

static void addr_map_init(struct nf_conn *ct, struct addr_map *map)
{
	struct nf_conntrack_tuple *t;
	enum ip_conntrack_dir dir;
	unsigned int n;

	for (dir = 0; dir < IP_CT_DIR_MAX; dir++) {
		t = &ct->tuplehash[dir].tuple;

		n = sprintf(map->addr[dir].src, "%u.%u.%u.%u",
			    NIPQUAD(t->src.u3.ip));
		map->addr[dir].srciplen = n;
		n += sprintf(map->addr[dir].src + n, ":%u",
			     ntohs(t->src.u.udp.port));
		map->addr[dir].srclen = n;

		n = sprintf(map->addr[dir].dst, "%u.%u.%u.%u",
			    NIPQUAD(t->dst.u3.ip));
		map->addr[dir].dstiplen = n;
		n += sprintf(map->addr[dir].dst + n, ":%u",
			     ntohs(t->dst.u.udp.port));
		map->addr[dir].dstlen = n;
	}
}

static int map_sip_addr(struct sk_buff **pskb, enum ip_conntrack_info ctinfo,
			struct nf_conn *ct, const char **dptr, size_t dlen,
			enum sip_header_pos pos, struct addr_map *map)
{
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	unsigned int matchlen, matchoff, addrlen;
	char *addr;

	if (ct_sip_get_info(ct, *dptr, dlen, &matchoff, &matchlen, pos) <= 0)
		return 1;

	if ((matchlen == map->addr[dir].srciplen ||
	     matchlen == map->addr[dir].srclen) &&
	    memcmp(*dptr + matchoff, map->addr[dir].src, matchlen) == 0) {
		addr    = map->addr[!dir].dst;
		addrlen = map->addr[!dir].dstlen;
	} else if ((matchlen == map->addr[dir].dstiplen ||
		    matchlen == map->addr[dir].dstlen) &&
		   memcmp(*dptr + matchoff, map->addr[dir].dst, matchlen) == 0) {
		addr    = map->addr[!dir].src;
		addrlen = map->addr[!dir].srclen;
	} else
		return 1;

	if (!nf_nat_mangle_udp_packet(pskb, ct, ctinfo,
				      matchoff, matchlen, addr, addrlen))
		return 0;
	*dptr = (*pskb)->data + ip_hdrlen(*pskb) + sizeof(struct udphdr);
	return 1;

}

static unsigned int ip_nat_sip(struct sk_buff **pskb,
			       enum ip_conntrack_info ctinfo,
			       struct nf_conn *ct,
			       const char **dptr)
{
	enum sip_header_pos pos;
	struct addr_map map;
	int dataoff, datalen;

	dataoff = ip_hdrlen(*pskb) + sizeof(struct udphdr);
	datalen = (*pskb)->len - dataoff;
	if (datalen < sizeof("SIP/2.0") - 1)
		return NF_DROP;

	addr_map_init(ct, &map);

	/* Basic rules: requests and responses. */
	if (strncmp(*dptr, "SIP/2.0", sizeof("SIP/2.0") - 1) != 0) {
		/* 10.2: Constructing the REGISTER Request:
		 *
		 * The "userinfo" and "@" components of the SIP URI MUST NOT
		 * be present.
		 */
		if (datalen >= sizeof("REGISTER") - 1 &&
		    strncmp(*dptr, "REGISTER", sizeof("REGISTER") - 1) == 0)
			pos = POS_REG_REQ_URI;
		else
			pos = POS_REQ_URI;

		if (!map_sip_addr(pskb, ctinfo, ct, dptr, datalen, pos, &map))
			return NF_DROP;
	}

	if (!map_sip_addr(pskb, ctinfo, ct, dptr, datalen, POS_FROM, &map) ||
	    !map_sip_addr(pskb, ctinfo, ct, dptr, datalen, POS_TO, &map) ||
	    !map_sip_addr(pskb, ctinfo, ct, dptr, datalen, POS_VIA, &map) ||
	    !map_sip_addr(pskb, ctinfo, ct, dptr, datalen, POS_CONTACT, &map))
		return NF_DROP;
	return NF_ACCEPT;
}

static unsigned int mangle_sip_packet(struct sk_buff **pskb,
				      enum ip_conntrack_info ctinfo,
				      struct nf_conn *ct,
				      const char **dptr, size_t dlen,
				      char *buffer, int bufflen,
				      enum sip_header_pos pos)
{
	unsigned int matchlen, matchoff;

	if (ct_sip_get_info(ct, *dptr, dlen, &matchoff, &matchlen, pos) <= 0)
		return 0;

	if (!nf_nat_mangle_udp_packet(pskb, ct, ctinfo,
				      matchoff, matchlen, buffer, bufflen))
		return 0;

	/* We need to reload this. Thanks Patrick. */
	*dptr = (*pskb)->data + ip_hdrlen(*pskb) + sizeof(struct udphdr);
	return 1;
}

static int mangle_content_len(struct sk_buff **pskb,
			      enum ip_conntrack_info ctinfo,
			      struct nf_conn *ct,
			      const char *dptr)
{
	unsigned int dataoff, matchoff, matchlen;
	char buffer[sizeof("65536")];
	int bufflen;

	dataoff = ip_hdrlen(*pskb) + sizeof(struct udphdr);

	/* Get actual SDP lenght */
	if (ct_sip_get_info(ct, dptr, (*pskb)->len - dataoff, &matchoff,
			    &matchlen, POS_SDP_HEADER) > 0) {

		/* since ct_sip_get_info() give us a pointer passing 'v='
		   we need to add 2 bytes in this count. */
		int c_len = (*pskb)->len - dataoff - matchoff + 2;

		/* Now, update SDP length */
		if (ct_sip_get_info(ct, dptr, (*pskb)->len - dataoff, &matchoff,
				    &matchlen, POS_CONTENT) > 0) {

			bufflen = sprintf(buffer, "%u", c_len);
			return nf_nat_mangle_udp_packet(pskb, ct, ctinfo,
							matchoff, matchlen,
							buffer, bufflen);
		}
	}
	return 0;
}

static unsigned int mangle_sdp(struct sk_buff **pskb,
			       enum ip_conntrack_info ctinfo,
			       struct nf_conn *ct,
			       __be32 newip, u_int16_t port,
			       const char *dptr)
{
	char buffer[sizeof("nnn.nnn.nnn.nnn")];
	unsigned int dataoff, bufflen;

	dataoff = ip_hdrlen(*pskb) + sizeof(struct udphdr);

	/* Mangle owner and contact info. */
	bufflen = sprintf(buffer, "%u.%u.%u.%u", NIPQUAD(newip));
	if (!mangle_sip_packet(pskb, ctinfo, ct, &dptr, (*pskb)->len - dataoff,
			       buffer, bufflen, POS_OWNER_IP4))
		return 0;

	if (!mangle_sip_packet(pskb, ctinfo, ct, &dptr, (*pskb)->len - dataoff,
			       buffer, bufflen, POS_CONNECTION_IP4))
		return 0;

	/* Mangle media port. */
	bufflen = sprintf(buffer, "%u", port);
	if (!mangle_sip_packet(pskb, ctinfo, ct, &dptr, (*pskb)->len - dataoff,
			       buffer, bufflen, POS_MEDIA))
		return 0;

	return mangle_content_len(pskb, ctinfo, ct, dptr);
}

static void ip_nat_sdp_expect(struct nf_conn *ct,
			      struct nf_conntrack_expect *exp)
{
	struct nf_nat_range range;

	/* This must be a fresh one. */
	BUG_ON(ct->status & IPS_NAT_DONE_MASK);

	/* Change src to where master sends to */
	range.flags = IP_NAT_RANGE_MAP_IPS;
	range.min_ip = range.max_ip
		= ct->master->tuplehash[!exp->dir].tuple.dst.u3.ip;
	/* hook doesn't matter, but it has to do source manip */
	nf_nat_setup_info(ct, &range, NF_IP_POST_ROUTING);

	/* For DST manip, map port here to where it's expected. */
	range.flags = (IP_NAT_RANGE_MAP_IPS | IP_NAT_RANGE_PROTO_SPECIFIED);
	range.min = range.max = exp->saved_proto;
	range.min_ip = range.max_ip = exp->saved_ip;
	/* hook doesn't matter, but it has to do destination manip */
	nf_nat_setup_info(ct, &range, NF_IP_PRE_ROUTING);
}

/* So, this packet has hit the connection tracking matching code.
   Mangle it, and change the expectation to match the new version. */
static unsigned int ip_nat_sdp(struct sk_buff **pskb,
			       enum ip_conntrack_info ctinfo,
			       struct nf_conntrack_expect *exp,
			       const char *dptr)
{
	struct nf_conn *ct = exp->master;
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	__be32 newip;
	u_int16_t port;

	/* Connection will come from reply */
	if (ct->tuplehash[dir].tuple.src.u3.ip ==
	    ct->tuplehash[!dir].tuple.dst.u3.ip)
		newip = exp->tuple.dst.u3.ip;
	else
		newip = ct->tuplehash[!dir].tuple.dst.u3.ip;

	exp->saved_ip = exp->tuple.dst.u3.ip;
	exp->tuple.dst.u3.ip = newip;
	exp->saved_proto.udp.port = exp->tuple.dst.u.udp.port;
	exp->dir = !dir;

	/* When you see the packet, we need to NAT it the same as the
	   this one. */
	exp->expectfn = ip_nat_sdp_expect;

	/* Try to get same port: if not, try to change it. */
	for (port = ntohs(exp->saved_proto.udp.port); port != 0; port++) {
		exp->tuple.dst.u.udp.port = htons(port);
		if (nf_ct_expect_related(exp) == 0)
			break;
	}

	if (port == 0)
		return NF_DROP;

	if (!mangle_sdp(pskb, ctinfo, ct, newip, port, dptr)) {
		nf_ct_unexpect_related(exp);
		return NF_DROP;
	}
	return NF_ACCEPT;
}

static void __exit nf_nat_sip_fini(void)
{
	rcu_assign_pointer(nf_nat_sip_hook, NULL);
	rcu_assign_pointer(nf_nat_sdp_hook, NULL);
	synchronize_rcu();
}

static int __init nf_nat_sip_init(void)
{
	BUG_ON(rcu_dereference(nf_nat_sip_hook));
	BUG_ON(rcu_dereference(nf_nat_sdp_hook));
	rcu_assign_pointer(nf_nat_sip_hook, ip_nat_sip);
	rcu_assign_pointer(nf_nat_sdp_hook, ip_nat_sdp);
	return 0;
}

module_init(nf_nat_sip_init);
module_exit(nf_nat_sip_fini);
