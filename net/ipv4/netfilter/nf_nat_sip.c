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

static void addr_map_init(const struct nf_conn *ct, struct addr_map *map)
{
	const struct nf_conntrack_tuple *t;
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

static unsigned int mangle_packet(struct sk_buff *skb,
				  const char **dptr, unsigned int *datalen,
				  unsigned int matchoff, unsigned int matchlen,
				  const char *buffer, unsigned int buflen)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);

	if (!nf_nat_mangle_udp_packet(skb, ct, ctinfo, matchoff, matchlen,
				      buffer, buflen))
		return 0;

	/* Reload data pointer and adjust datalen value */
	*dptr = skb->data + ip_hdrlen(skb) + sizeof(struct udphdr);
	*datalen += buflen - matchlen;
	return 1;
}

static int map_sip_addr(struct sk_buff *skb,
			const char **dptr, unsigned int *datalen,
			enum sip_header_pos pos, struct addr_map *map)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	unsigned int matchlen, matchoff, addrlen;
	char *addr;

	if (ct_sip_get_info(ct, *dptr, *datalen, &matchoff, &matchlen,
			    pos) <= 0)
		return 1;

	if ((matchlen == map->addr[dir].srciplen ||
	     matchlen == map->addr[dir].srclen) &&
	    strncmp(*dptr + matchoff, map->addr[dir].src, matchlen) == 0) {
		addr    = map->addr[!dir].dst;
		addrlen = map->addr[!dir].dstlen;
	} else if ((matchlen == map->addr[dir].dstiplen ||
		    matchlen == map->addr[dir].dstlen) &&
		   strncmp(*dptr + matchoff, map->addr[dir].dst, matchlen) == 0) {
		addr    = map->addr[!dir].src;
		addrlen = map->addr[!dir].srclen;
	} else
		return 1;

	return mangle_packet(skb, dptr, datalen, matchoff, matchlen,
			     addr, addrlen);
}

static unsigned int ip_nat_sip(struct sk_buff *skb,
			       const char **dptr, unsigned int *datalen)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);
	enum sip_header_pos pos;
	struct addr_map map;

	if (*datalen < strlen("SIP/2.0"))
		return NF_ACCEPT;

	addr_map_init(ct, &map);

	/* Basic rules: requests and responses. */
	if (strnicmp(*dptr, "SIP/2.0", strlen("SIP/2.0")) != 0) {
		/* 10.2: Constructing the REGISTER Request:
		 *
		 * The "userinfo" and "@" components of the SIP URI MUST NOT
		 * be present.
		 */
		if (*datalen >= strlen("REGISTER") &&
		    strnicmp(*dptr, "REGISTER", strlen("REGISTER")) == 0)
			pos = POS_REG_REQ_URI;
		else
			pos = POS_REQ_URI;

		if (!map_sip_addr(skb, dptr, datalen, pos, &map))
			return NF_DROP;
	}

	if (!map_sip_addr(skb, dptr, datalen, POS_FROM, &map) ||
	    !map_sip_addr(skb, dptr, datalen, POS_TO, &map) ||
	    !map_sip_addr(skb, dptr, datalen, POS_VIA, &map) ||
	    !map_sip_addr(skb, dptr, datalen, POS_CONTACT, &map))
		return NF_DROP;
	return NF_ACCEPT;
}

static int mangle_content_len(struct sk_buff *skb,
			      const char **dptr, unsigned int *datalen)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);
	unsigned int matchoff, matchlen;
	char buffer[sizeof("65536")];
	int buflen, c_len;

	/* Get actual SDP length */
	if (ct_sip_get_sdp_header(ct, *dptr, 0, *datalen,
				  SDP_HDR_VERSION, SDP_HDR_UNSPEC,
				  &matchoff, &matchlen) <= 0)
		return 0;
	c_len = *datalen - matchoff + strlen("v=");

	/* Now, update SDP length */
	if (ct_sip_get_info(ct, *dptr, *datalen, &matchoff, &matchlen,
			    POS_CONTENT) <= 0)
		return 0;

	buflen = sprintf(buffer, "%u", c_len);
	return mangle_packet(skb, dptr, datalen, matchoff, matchlen,
			     buffer, buflen);
}

static unsigned mangle_sdp_packet(struct sk_buff *skb,
				  const char **dptr, unsigned int *datalen,
				  enum sdp_header_types type,
				  char *buffer, int buflen)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);
	unsigned int matchlen, matchoff;

	if (ct_sip_get_sdp_header(ct, *dptr, 0, *datalen, type, SDP_HDR_UNSPEC,
				  &matchoff, &matchlen) <= 0)
		return 0;
	return mangle_packet(skb, dptr, datalen, matchoff, matchlen,
			     buffer, buflen);
}

static unsigned int mangle_sdp(struct sk_buff *skb,
			       enum ip_conntrack_info ctinfo,
			       struct nf_conn *ct,
			       __be32 newip, u_int16_t port,
			       const char **dptr, unsigned int *datalen)
{
	char buffer[sizeof("nnn.nnn.nnn.nnn")];
	unsigned int bufflen;

	/* Mangle owner and contact info. */
	bufflen = sprintf(buffer, "%u.%u.%u.%u", NIPQUAD(newip));
	if (!mangle_sdp_packet(skb, dptr, datalen, SDP_HDR_OWNER_IP4,
			       buffer, bufflen))
		return 0;

	if (!mangle_sdp_packet(skb, dptr, datalen, SDP_HDR_CONNECTION_IP4,
			       buffer, bufflen))
		return 0;

	/* Mangle media port. */
	bufflen = sprintf(buffer, "%u", port);
	if (!mangle_sdp_packet(skb, dptr, datalen, SDP_HDR_MEDIA,
			       buffer, bufflen))
		return 0;

	return mangle_content_len(skb, dptr, datalen);
}

static void ip_nat_sdp_expect(struct nf_conn *ct,
			      struct nf_conntrack_expect *exp)
{
	struct nf_nat_range range;

	/* This must be a fresh one. */
	BUG_ON(ct->status & IPS_NAT_DONE_MASK);

	/* For DST manip, map port here to where it's expected. */
	range.flags = (IP_NAT_RANGE_MAP_IPS | IP_NAT_RANGE_PROTO_SPECIFIED);
	range.min = range.max = exp->saved_proto;
	range.min_ip = range.max_ip = exp->saved_ip;
	nf_nat_setup_info(ct, &range, IP_NAT_MANIP_DST);

	/* Change src to where master sends to */
	range.flags = IP_NAT_RANGE_MAP_IPS;
	range.min_ip = range.max_ip
		= ct->master->tuplehash[!exp->dir].tuple.dst.u3.ip;
	nf_nat_setup_info(ct, &range, IP_NAT_MANIP_SRC);
}

/* So, this packet has hit the connection tracking matching code.
   Mangle it, and change the expectation to match the new version. */
static unsigned int ip_nat_sdp(struct sk_buff *skb,
			       const char **dptr, unsigned int *datalen,
			       struct nf_conntrack_expect *exp)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);
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

	if (!mangle_sdp(skb, ctinfo, ct, newip, port, dptr, datalen)) {
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
	BUG_ON(nf_nat_sip_hook != NULL);
	BUG_ON(nf_nat_sdp_hook != NULL);
	rcu_assign_pointer(nf_nat_sip_hook, ip_nat_sip);
	rcu_assign_pointer(nf_nat_sdp_hook, ip_nat_sdp);
	return 0;
}

module_init(nf_nat_sip_init);
module_exit(nf_nat_sip_fini);
