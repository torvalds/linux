/* SIP extension for UDP NAT alteration.
 *
 * (C) 2005 by Christian Hentschel <chentschel@arnet.com.ar>
 * based on RR's ip_nat_ftp.c and other modules.
 * (C) 2007 United Security Providers
 * (C) 2007, 2008 Patrick McHardy <kaber@trash.net>
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

static int map_addr(struct sk_buff *skb,
		    const char **dptr, unsigned int *datalen,
		    unsigned int matchoff, unsigned int matchlen,
		    union nf_inet_addr *addr, __be16 port)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	char buffer[sizeof("nnn.nnn.nnn.nnn:nnnnn")];
	unsigned int buflen;
	__be32 newaddr;
	__be16 newport;

	if (ct->tuplehash[dir].tuple.src.u3.ip == addr->ip &&
	    ct->tuplehash[dir].tuple.src.u.udp.port == port) {
		newaddr = ct->tuplehash[!dir].tuple.dst.u3.ip;
		newport = ct->tuplehash[!dir].tuple.dst.u.udp.port;
	} else if (ct->tuplehash[dir].tuple.dst.u3.ip == addr->ip &&
		   ct->tuplehash[dir].tuple.dst.u.udp.port == port) {
		newaddr = ct->tuplehash[!dir].tuple.src.u3.ip;
		newport = ct->tuplehash[!dir].tuple.src.u.udp.port;
	} else
		return 1;

	if (newaddr == addr->ip && newport == port)
		return 1;

	buflen = sprintf(buffer, "%u.%u.%u.%u:%u",
			 NIPQUAD(newaddr), ntohs(newport));

	return mangle_packet(skb, dptr, datalen, matchoff, matchlen,
			     buffer, buflen);
}

static int map_sip_addr(struct sk_buff *skb,
			const char **dptr, unsigned int *datalen,
			enum sip_header_types type)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);
	unsigned int matchlen, matchoff;
	union nf_inet_addr addr;
	__be16 port;

	if (ct_sip_parse_header_uri(ct, *dptr, NULL, *datalen, type, NULL,
				    &matchoff, &matchlen, &addr, &port) <= 0)
		return 1;
	return map_addr(skb, dptr, datalen, matchoff, matchlen, &addr, port);
}

static unsigned int ip_nat_sip(struct sk_buff *skb,
			       const char **dptr, unsigned int *datalen)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	unsigned int dataoff, matchoff, matchlen;
	union nf_inet_addr addr;
	__be16 port;
	int request, in_header;

	/* Basic rules: requests and responses. */
	if (strnicmp(*dptr, "SIP/2.0", strlen("SIP/2.0")) != 0) {
		if (ct_sip_parse_request(ct, *dptr, *datalen,
					 &matchoff, &matchlen,
					 &addr, &port) > 0 &&
		    !map_addr(skb, dptr, datalen, matchoff, matchlen,
			      &addr, port))
			return NF_DROP;
		request = 1;
	} else
		request = 0;

	/* Translate topmost Via header and parameters */
	if (ct_sip_parse_header_uri(ct, *dptr, NULL, *datalen,
				    SIP_HDR_VIA, NULL, &matchoff, &matchlen,
				    &addr, &port) > 0) {
		unsigned int matchend, poff, plen, buflen, n;
		char buffer[sizeof("nnn.nnn.nnn.nnn:nnnnn")];

		/* We're only interested in headers related to this
		 * connection */
		if (request) {
			if (addr.ip != ct->tuplehash[dir].tuple.src.u3.ip ||
			    port != ct->tuplehash[dir].tuple.src.u.udp.port)
				goto next;
		} else {
			if (addr.ip != ct->tuplehash[dir].tuple.dst.u3.ip ||
			    port != ct->tuplehash[dir].tuple.dst.u.udp.port)
				goto next;
		}

		if (!map_addr(skb, dptr, datalen, matchoff, matchlen,
			      &addr, port))
			return NF_DROP;

		matchend = matchoff + matchlen;

		/* The maddr= parameter (RFC 2361) specifies where to send
		 * the reply. */
		if (ct_sip_parse_address_param(ct, *dptr, matchend, *datalen,
					       "maddr=", &poff, &plen,
					       &addr) > 0 &&
		    addr.ip == ct->tuplehash[dir].tuple.src.u3.ip &&
		    addr.ip != ct->tuplehash[!dir].tuple.dst.u3.ip) {
			__be32 ip = ct->tuplehash[!dir].tuple.dst.u3.ip;
			buflen = sprintf(buffer, "%u.%u.%u.%u", NIPQUAD(ip));
			if (!mangle_packet(skb, dptr, datalen, poff, plen,
					   buffer, buflen))
				return NF_DROP;
		}

		/* The received= parameter (RFC 2361) contains the address
		 * from which the server received the request. */
		if (ct_sip_parse_address_param(ct, *dptr, matchend, *datalen,
					       "received=", &poff, &plen,
					       &addr) > 0 &&
		    addr.ip == ct->tuplehash[dir].tuple.dst.u3.ip &&
		    addr.ip != ct->tuplehash[!dir].tuple.src.u3.ip) {
			__be32 ip = ct->tuplehash[!dir].tuple.src.u3.ip;
			buflen = sprintf(buffer, "%u.%u.%u.%u", NIPQUAD(ip));
			if (!mangle_packet(skb, dptr, datalen, poff, plen,
					   buffer, buflen))
				return NF_DROP;
		}

		/* The rport= parameter (RFC 3581) contains the port number
		 * from which the server received the request. */
		if (ct_sip_parse_numerical_param(ct, *dptr, matchend, *datalen,
						 "rport=", &poff, &plen,
						 &n) > 0 &&
		    htons(n) == ct->tuplehash[dir].tuple.dst.u.udp.port &&
		    htons(n) != ct->tuplehash[!dir].tuple.src.u.udp.port) {
			__be16 p = ct->tuplehash[!dir].tuple.src.u.udp.port;
			buflen = sprintf(buffer, "%u", ntohs(p));
			if (!mangle_packet(skb, dptr, datalen, poff, plen,
					   buffer, buflen))
				return NF_DROP;
		}
	}

next:
	/* Translate Contact headers */
	dataoff = 0;
	in_header = 0;
	while (ct_sip_parse_header_uri(ct, *dptr, &dataoff, *datalen,
				       SIP_HDR_CONTACT, &in_header,
				       &matchoff, &matchlen,
				       &addr, &port) > 0) {
		if (!map_addr(skb, dptr, datalen, matchoff, matchlen,
			      &addr, port))
			return NF_DROP;
	}

	if (!map_sip_addr(skb, dptr, datalen, SIP_HDR_FROM) ||
	    !map_sip_addr(skb, dptr, datalen, SIP_HDR_TO))
		return NF_DROP;
	return NF_ACCEPT;
}

/* Handles expected signalling connections and media streams */
static void ip_nat_sip_expected(struct nf_conn *ct,
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

	/* Change src to where master sends to, but only if the connection
	 * actually came from the same source. */
	if (ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u3.ip ==
	    ct->master->tuplehash[exp->dir].tuple.src.u3.ip) {
		range.flags = IP_NAT_RANGE_MAP_IPS;
		range.min_ip = range.max_ip
			= ct->master->tuplehash[!exp->dir].tuple.dst.u3.ip;
		nf_nat_setup_info(ct, &range, IP_NAT_MANIP_SRC);
	}
}

static unsigned int ip_nat_sip_expect(struct sk_buff *skb,
				      const char **dptr, unsigned int *datalen,
				      struct nf_conntrack_expect *exp,
				      unsigned int matchoff,
				      unsigned int matchlen)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	__be32 newip;
	u_int16_t port;
	char buffer[sizeof("nnn.nnn.nnn.nnn:nnnnn")];
	unsigned buflen;

	/* Connection will come from reply */
	if (ct->tuplehash[dir].tuple.src.u3.ip == ct->tuplehash[!dir].tuple.dst.u3.ip)
		newip = exp->tuple.dst.u3.ip;
	else
		newip = ct->tuplehash[!dir].tuple.dst.u3.ip;

	/* If the signalling port matches the connection's source port in the
	 * original direction, try to use the destination port in the opposite
	 * direction. */
	if (exp->tuple.dst.u.udp.port ==
	    ct->tuplehash[dir].tuple.src.u.udp.port)
		port = ntohs(ct->tuplehash[!dir].tuple.dst.u.udp.port);
	else
		port = ntohs(exp->tuple.dst.u.udp.port);

	exp->saved_ip = exp->tuple.dst.u3.ip;
	exp->tuple.dst.u3.ip = newip;
	exp->saved_proto.udp.port = exp->tuple.dst.u.udp.port;
	exp->dir = !dir;
	exp->expectfn = ip_nat_sip_expected;

	for (; port != 0; port++) {
		exp->tuple.dst.u.udp.port = htons(port);
		if (nf_ct_expect_related(exp) == 0)
			break;
	}

	if (port == 0)
		return NF_DROP;

	if (exp->tuple.dst.u3.ip != exp->saved_ip ||
	    exp->tuple.dst.u.udp.port != exp->saved_proto.udp.port) {
		buflen = sprintf(buffer, "%u.%u.%u.%u:%u",
				 NIPQUAD(newip), port);
		if (!mangle_packet(skb, dptr, datalen, matchoff, matchlen,
				   buffer, buflen))
			goto err;
	}
	return NF_ACCEPT;

err:
	nf_ct_unexpect_related(exp);
	return NF_DROP;
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
	if (ct_sip_get_header(ct, *dptr, 0, *datalen, SIP_HDR_CONTENT_LENGTH,
			      &matchoff, &matchlen) <= 0)
		return 0;

	buflen = sprintf(buffer, "%u", c_len);
	return mangle_packet(skb, dptr, datalen, matchoff, matchlen,
			     buffer, buflen);
}

static unsigned mangle_sdp_packet(struct sk_buff *skb, const char **dptr,
				  unsigned int dataoff, unsigned int *datalen,
				  enum sdp_header_types type,
				  enum sdp_header_types term,
				  char *buffer, int buflen)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);
	unsigned int matchlen, matchoff;

	if (ct_sip_get_sdp_header(ct, *dptr, dataoff, *datalen, type, term,
				  &matchoff, &matchlen) <= 0)
		return 0;
	return mangle_packet(skb, dptr, datalen, matchoff, matchlen,
			     buffer, buflen);
}

static unsigned int ip_nat_sdp_addr(struct sk_buff *skb, const char **dptr,
				    unsigned int dataoff,
				    unsigned int *datalen,
				    enum sdp_header_types type,
				    enum sdp_header_types term,
				    const union nf_inet_addr *addr)
{
	char buffer[sizeof("nnn.nnn.nnn.nnn")];
	unsigned int buflen;

	buflen = sprintf(buffer, NIPQUAD_FMT, NIPQUAD(addr->ip));
	if (!mangle_sdp_packet(skb, dptr, dataoff, datalen, type, term,
			       buffer, buflen))
		return 0;

	return mangle_content_len(skb, dptr, datalen);
}

static unsigned int ip_nat_sdp_port(struct sk_buff *skb,
				    const char **dptr,
				    unsigned int *datalen,
				    unsigned int matchoff,
				    unsigned int matchlen,
				    u_int16_t port)
{
	char buffer[sizeof("nnnnn")];
	unsigned int buflen;

	buflen = sprintf(buffer, "%u", port);
	if (!mangle_packet(skb, dptr, datalen, matchoff, matchlen,
			   buffer, buflen))
		return 0;

	return mangle_content_len(skb, dptr, datalen);
}

static unsigned int ip_nat_sdp_session(struct sk_buff *skb, const char **dptr,
				       unsigned int dataoff,
				       unsigned int *datalen,
				       const union nf_inet_addr *addr)
{
	char buffer[sizeof("nnn.nnn.nnn.nnn")];
	unsigned int buflen;

	/* Mangle session description owner and contact addresses */
	buflen = sprintf(buffer, "%u.%u.%u.%u", NIPQUAD(addr->ip));
	if (!mangle_sdp_packet(skb, dptr, dataoff, datalen,
			       SDP_HDR_OWNER_IP4, SDP_HDR_MEDIA,
			       buffer, buflen))
		return 0;

	if (!mangle_sdp_packet(skb, dptr, dataoff, datalen,
			       SDP_HDR_CONNECTION_IP4, SDP_HDR_MEDIA,
			       buffer, buflen))
		return 0;

	return mangle_content_len(skb, dptr, datalen);
}

/* So, this packet has hit the connection tracking matching code.
   Mangle it, and change the expectation to match the new version. */
static unsigned int ip_nat_sdp_media(struct sk_buff *skb,
				     const char **dptr,
				     unsigned int *datalen,
				     struct nf_conntrack_expect *rtp_exp,
				     struct nf_conntrack_expect *rtcp_exp,
				     unsigned int mediaoff,
				     unsigned int medialen,
				     union nf_inet_addr *rtp_addr)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	u_int16_t port;

	/* Connection will come from reply */
	if (ct->tuplehash[dir].tuple.src.u3.ip ==
	    ct->tuplehash[!dir].tuple.dst.u3.ip)
		rtp_addr->ip = rtp_exp->tuple.dst.u3.ip;
	else
		rtp_addr->ip = ct->tuplehash[!dir].tuple.dst.u3.ip;

	rtp_exp->saved_ip = rtp_exp->tuple.dst.u3.ip;
	rtp_exp->tuple.dst.u3.ip = rtp_addr->ip;
	rtp_exp->saved_proto.udp.port = rtp_exp->tuple.dst.u.udp.port;
	rtp_exp->dir = !dir;
	rtp_exp->expectfn = ip_nat_sip_expected;

	rtcp_exp->saved_ip = rtcp_exp->tuple.dst.u3.ip;
	rtcp_exp->tuple.dst.u3.ip = rtp_addr->ip;
	rtcp_exp->saved_proto.udp.port = rtcp_exp->tuple.dst.u.udp.port;
	rtcp_exp->dir = !dir;
	rtcp_exp->expectfn = ip_nat_sip_expected;

	/* Try to get same pair of ports: if not, try to change them. */
	for (port = ntohs(rtp_exp->tuple.dst.u.udp.port);
	     port != 0; port += 2) {
		rtp_exp->tuple.dst.u.udp.port = htons(port);
		if (nf_ct_expect_related(rtp_exp) != 0)
			continue;
		rtcp_exp->tuple.dst.u.udp.port = htons(port + 1);
		if (nf_ct_expect_related(rtcp_exp) == 0)
			break;
		nf_ct_unexpect_related(rtp_exp);
	}

	if (port == 0)
		goto err1;

	/* Update media port. */
	if (rtp_exp->tuple.dst.u.udp.port != rtp_exp->saved_proto.udp.port &&
	    !ip_nat_sdp_port(skb, dptr, datalen, mediaoff, medialen, port))
		goto err2;

	return NF_ACCEPT;

err2:
	nf_ct_unexpect_related(rtp_exp);
	nf_ct_unexpect_related(rtcp_exp);
err1:
	return NF_DROP;
}

static void __exit nf_nat_sip_fini(void)
{
	rcu_assign_pointer(nf_nat_sip_hook, NULL);
	rcu_assign_pointer(nf_nat_sip_expect_hook, NULL);
	rcu_assign_pointer(nf_nat_sdp_addr_hook, NULL);
	rcu_assign_pointer(nf_nat_sdp_port_hook, NULL);
	rcu_assign_pointer(nf_nat_sdp_session_hook, NULL);
	rcu_assign_pointer(nf_nat_sdp_media_hook, NULL);
	synchronize_rcu();
}

static int __init nf_nat_sip_init(void)
{
	BUG_ON(nf_nat_sip_hook != NULL);
	BUG_ON(nf_nat_sip_expect_hook != NULL);
	BUG_ON(nf_nat_sdp_addr_hook != NULL);
	BUG_ON(nf_nat_sdp_port_hook != NULL);
	BUG_ON(nf_nat_sdp_session_hook != NULL);
	BUG_ON(nf_nat_sdp_media_hook != NULL);
	rcu_assign_pointer(nf_nat_sip_hook, ip_nat_sip);
	rcu_assign_pointer(nf_nat_sip_expect_hook, ip_nat_sip_expect);
	rcu_assign_pointer(nf_nat_sdp_addr_hook, ip_nat_sdp_addr);
	rcu_assign_pointer(nf_nat_sdp_port_hook, ip_nat_sdp_port);
	rcu_assign_pointer(nf_nat_sdp_session_hook, ip_nat_sdp_session);
	rcu_assign_pointer(nf_nat_sdp_media_hook, ip_nat_sdp_media);
	return 0;
}

module_init(nf_nat_sip_init);
module_exit(nf_nat_sip_fini);
