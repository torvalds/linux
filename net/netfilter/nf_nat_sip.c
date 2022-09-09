// SPDX-License-Identifier: GPL-2.0-only
/* SIP extension for NAT alteration.
 *
 * (C) 2005 by Christian Hentschel <chentschel@arnet.com.ar>
 * based on RR's ip_nat_ftp.c and other modules.
 * (C) 2007 United Security Providers
 * (C) 2007, 2008, 2011, 2012 Patrick McHardy <kaber@trash.net>
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/inet.h>
#include <linux/udp.h>
#include <linux/tcp.h>

#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_nat_helper.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_expect.h>
#include <net/netfilter/nf_conntrack_seqadj.h>
#include <linux/netfilter/nf_conntrack_sip.h>

#define NAT_HELPER_NAME "sip"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian Hentschel <chentschel@arnet.com.ar>");
MODULE_DESCRIPTION("SIP NAT helper");
MODULE_ALIAS_NF_NAT_HELPER(NAT_HELPER_NAME);

static struct nf_conntrack_nat_helper nat_helper_sip =
	NF_CT_NAT_HELPER_INIT(NAT_HELPER_NAME);

static unsigned int mangle_packet(struct sk_buff *skb, unsigned int protoff,
				  unsigned int dataoff,
				  const char **dptr, unsigned int *datalen,
				  unsigned int matchoff, unsigned int matchlen,
				  const char *buffer, unsigned int buflen)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);
	struct tcphdr *th;
	unsigned int baseoff;

	if (nf_ct_protonum(ct) == IPPROTO_TCP) {
		th = (struct tcphdr *)(skb->data + protoff);
		baseoff = protoff + th->doff * 4;
		matchoff += dataoff - baseoff;

		if (!__nf_nat_mangle_tcp_packet(skb, ct, ctinfo,
						protoff, matchoff, matchlen,
						buffer, buflen, false))
			return 0;
	} else {
		baseoff = protoff + sizeof(struct udphdr);
		matchoff += dataoff - baseoff;

		if (!nf_nat_mangle_udp_packet(skb, ct, ctinfo,
					      protoff, matchoff, matchlen,
					      buffer, buflen))
			return 0;
	}

	/* Reload data pointer and adjust datalen value */
	*dptr = skb->data + dataoff;
	*datalen += buflen - matchlen;
	return 1;
}

static int sip_sprintf_addr(const struct nf_conn *ct, char *buffer,
			    const union nf_inet_addr *addr, bool delim)
{
	if (nf_ct_l3num(ct) == NFPROTO_IPV4)
		return sprintf(buffer, "%pI4", &addr->ip);
	else {
		if (delim)
			return sprintf(buffer, "[%pI6c]", &addr->ip6);
		else
			return sprintf(buffer, "%pI6c", &addr->ip6);
	}
}

static int sip_sprintf_addr_port(const struct nf_conn *ct, char *buffer,
				 const union nf_inet_addr *addr, u16 port)
{
	if (nf_ct_l3num(ct) == NFPROTO_IPV4)
		return sprintf(buffer, "%pI4:%u", &addr->ip, port);
	else
		return sprintf(buffer, "[%pI6c]:%u", &addr->ip6, port);
}

static int map_addr(struct sk_buff *skb, unsigned int protoff,
		    unsigned int dataoff,
		    const char **dptr, unsigned int *datalen,
		    unsigned int matchoff, unsigned int matchlen,
		    union nf_inet_addr *addr, __be16 port)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	struct nf_ct_sip_master *ct_sip_info = nfct_help_data(ct);
	char buffer[INET6_ADDRSTRLEN + sizeof("[]:nnnnn")];
	unsigned int buflen;
	union nf_inet_addr newaddr;
	__be16 newport;

	if (nf_inet_addr_cmp(&ct->tuplehash[dir].tuple.src.u3, addr) &&
	    ct->tuplehash[dir].tuple.src.u.udp.port == port) {
		newaddr = ct->tuplehash[!dir].tuple.dst.u3;
		newport = ct->tuplehash[!dir].tuple.dst.u.udp.port;
	} else if (nf_inet_addr_cmp(&ct->tuplehash[dir].tuple.dst.u3, addr) &&
		   ct->tuplehash[dir].tuple.dst.u.udp.port == port) {
		newaddr = ct->tuplehash[!dir].tuple.src.u3;
		newport = ct_sip_info->forced_dport ? :
			  ct->tuplehash[!dir].tuple.src.u.udp.port;
	} else
		return 1;

	if (nf_inet_addr_cmp(&newaddr, addr) && newport == port)
		return 1;

	buflen = sip_sprintf_addr_port(ct, buffer, &newaddr, ntohs(newport));
	return mangle_packet(skb, protoff, dataoff, dptr, datalen,
			     matchoff, matchlen, buffer, buflen);
}

static int map_sip_addr(struct sk_buff *skb, unsigned int protoff,
			unsigned int dataoff,
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
	return map_addr(skb, protoff, dataoff, dptr, datalen,
			matchoff, matchlen, &addr, port);
}

static unsigned int nf_nat_sip(struct sk_buff *skb, unsigned int protoff,
			       unsigned int dataoff,
			       const char **dptr, unsigned int *datalen)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	struct nf_ct_sip_master *ct_sip_info = nfct_help_data(ct);
	unsigned int coff, matchoff, matchlen;
	enum sip_header_types hdr;
	union nf_inet_addr addr;
	__be16 port;
	int request, in_header;

	/* Basic rules: requests and responses. */
	if (strncasecmp(*dptr, "SIP/2.0", strlen("SIP/2.0")) != 0) {
		if (ct_sip_parse_request(ct, *dptr, *datalen,
					 &matchoff, &matchlen,
					 &addr, &port) > 0 &&
		    !map_addr(skb, protoff, dataoff, dptr, datalen,
			      matchoff, matchlen, &addr, port)) {
			nf_ct_helper_log(skb, ct, "cannot mangle SIP message");
			return NF_DROP;
		}
		request = 1;
	} else
		request = 0;

	if (nf_ct_protonum(ct) == IPPROTO_TCP)
		hdr = SIP_HDR_VIA_TCP;
	else
		hdr = SIP_HDR_VIA_UDP;

	/* Translate topmost Via header and parameters */
	if (ct_sip_parse_header_uri(ct, *dptr, NULL, *datalen,
				    hdr, NULL, &matchoff, &matchlen,
				    &addr, &port) > 0) {
		unsigned int olen, matchend, poff, plen, buflen, n;
		char buffer[INET6_ADDRSTRLEN + sizeof("[]:nnnnn")];

		/* We're only interested in headers related to this
		 * connection */
		if (request) {
			if (!nf_inet_addr_cmp(&addr,
					&ct->tuplehash[dir].tuple.src.u3) ||
			    port != ct->tuplehash[dir].tuple.src.u.udp.port)
				goto next;
		} else {
			if (!nf_inet_addr_cmp(&addr,
					&ct->tuplehash[dir].tuple.dst.u3) ||
			    port != ct->tuplehash[dir].tuple.dst.u.udp.port)
				goto next;
		}

		olen = *datalen;
		if (!map_addr(skb, protoff, dataoff, dptr, datalen,
			      matchoff, matchlen, &addr, port)) {
			nf_ct_helper_log(skb, ct, "cannot mangle Via header");
			return NF_DROP;
		}

		matchend = matchoff + matchlen + *datalen - olen;

		/* The maddr= parameter (RFC 2361) specifies where to send
		 * the reply. */
		if (ct_sip_parse_address_param(ct, *dptr, matchend, *datalen,
					       "maddr=", &poff, &plen,
					       &addr, true) > 0 &&
		    nf_inet_addr_cmp(&addr, &ct->tuplehash[dir].tuple.src.u3) &&
		    !nf_inet_addr_cmp(&addr, &ct->tuplehash[!dir].tuple.dst.u3)) {
			buflen = sip_sprintf_addr(ct, buffer,
					&ct->tuplehash[!dir].tuple.dst.u3,
					true);
			if (!mangle_packet(skb, protoff, dataoff, dptr, datalen,
					   poff, plen, buffer, buflen)) {
				nf_ct_helper_log(skb, ct, "cannot mangle maddr");
				return NF_DROP;
			}
		}

		/* The received= parameter (RFC 2361) contains the address
		 * from which the server received the request. */
		if (ct_sip_parse_address_param(ct, *dptr, matchend, *datalen,
					       "received=", &poff, &plen,
					       &addr, false) > 0 &&
		    nf_inet_addr_cmp(&addr, &ct->tuplehash[dir].tuple.dst.u3) &&
		    !nf_inet_addr_cmp(&addr, &ct->tuplehash[!dir].tuple.src.u3)) {
			buflen = sip_sprintf_addr(ct, buffer,
					&ct->tuplehash[!dir].tuple.src.u3,
					false);
			if (!mangle_packet(skb, protoff, dataoff, dptr, datalen,
					   poff, plen, buffer, buflen)) {
				nf_ct_helper_log(skb, ct, "cannot mangle received");
				return NF_DROP;
			}
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
			if (!mangle_packet(skb, protoff, dataoff, dptr, datalen,
					   poff, plen, buffer, buflen)) {
				nf_ct_helper_log(skb, ct, "cannot mangle rport");
				return NF_DROP;
			}
		}
	}

next:
	/* Translate Contact headers */
	coff = 0;
	in_header = 0;
	while (ct_sip_parse_header_uri(ct, *dptr, &coff, *datalen,
				       SIP_HDR_CONTACT, &in_header,
				       &matchoff, &matchlen,
				       &addr, &port) > 0) {
		if (!map_addr(skb, protoff, dataoff, dptr, datalen,
			      matchoff, matchlen,
			      &addr, port)) {
			nf_ct_helper_log(skb, ct, "cannot mangle contact");
			return NF_DROP;
		}
	}

	if (!map_sip_addr(skb, protoff, dataoff, dptr, datalen, SIP_HDR_FROM) ||
	    !map_sip_addr(skb, protoff, dataoff, dptr, datalen, SIP_HDR_TO)) {
		nf_ct_helper_log(skb, ct, "cannot mangle SIP from/to");
		return NF_DROP;
	}

	/* Mangle destination port for Cisco phones, then fix up checksums */
	if (dir == IP_CT_DIR_REPLY && ct_sip_info->forced_dport) {
		struct udphdr *uh;

		if (skb_ensure_writable(skb, skb->len)) {
			nf_ct_helper_log(skb, ct, "cannot mangle packet");
			return NF_DROP;
		}

		uh = (void *)skb->data + protoff;
		uh->dest = ct_sip_info->forced_dport;

		if (!nf_nat_mangle_udp_packet(skb, ct, ctinfo, protoff,
					      0, 0, NULL, 0)) {
			nf_ct_helper_log(skb, ct, "cannot mangle packet");
			return NF_DROP;
		}
	}

	return NF_ACCEPT;
}

static void nf_nat_sip_seq_adjust(struct sk_buff *skb, unsigned int protoff,
				  s16 off)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);
	const struct tcphdr *th;

	if (nf_ct_protonum(ct) != IPPROTO_TCP || off == 0)
		return;

	th = (struct tcphdr *)(skb->data + protoff);
	nf_ct_seqadj_set(ct, ctinfo, th->seq, off);
}

/* Handles expected signalling connections and media streams */
static void nf_nat_sip_expected(struct nf_conn *ct,
				struct nf_conntrack_expect *exp)
{
	struct nf_conn_help *help = nfct_help(ct->master);
	struct nf_conntrack_expect *pair_exp;
	int range_set_for_snat = 0;
	struct nf_nat_range2 range;

	/* This must be a fresh one. */
	BUG_ON(ct->status & IPS_NAT_DONE_MASK);

	/* For DST manip, map port here to where it's expected. */
	range.flags = (NF_NAT_RANGE_MAP_IPS | NF_NAT_RANGE_PROTO_SPECIFIED);
	range.min_proto = range.max_proto = exp->saved_proto;
	range.min_addr = range.max_addr = exp->saved_addr;
	nf_nat_setup_info(ct, &range, NF_NAT_MANIP_DST);

	/* Do media streams SRC manip according with the parameters
	 * found in the paired expectation.
	 */
	if (exp->class != SIP_EXPECT_SIGNALLING) {
		spin_lock_bh(&nf_conntrack_expect_lock);
		hlist_for_each_entry(pair_exp, &help->expectations, lnode) {
			if (pair_exp->tuple.src.l3num == nf_ct_l3num(ct) &&
			    pair_exp->tuple.dst.protonum == ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.protonum &&
			    nf_inet_addr_cmp(&ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u3, &pair_exp->saved_addr) &&
			    ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u.all == pair_exp->saved_proto.all) {
				range.flags = (NF_NAT_RANGE_MAP_IPS | NF_NAT_RANGE_PROTO_SPECIFIED);
				range.min_proto.all = range.max_proto.all = pair_exp->tuple.dst.u.all;
				range.min_addr = range.max_addr = pair_exp->tuple.dst.u3;
				range_set_for_snat = 1;
				break;
			}
		}
		spin_unlock_bh(&nf_conntrack_expect_lock);
	}

	/* When no paired expectation has been found, change src to
	 * where master sends to, but only if the connection actually came
	 * from the same source.
	 */
	if (!range_set_for_snat &&
	    nf_inet_addr_cmp(&ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u3,
			     &ct->master->tuplehash[exp->dir].tuple.src.u3)) {
		range.flags = NF_NAT_RANGE_MAP_IPS;
		range.min_addr = range.max_addr
			= ct->master->tuplehash[!exp->dir].tuple.dst.u3;
		range_set_for_snat = 1;
	}

	/* Perform SRC manip. */
	if (range_set_for_snat)
		nf_nat_setup_info(ct, &range, NF_NAT_MANIP_SRC);
}

static unsigned int nf_nat_sip_expect(struct sk_buff *skb, unsigned int protoff,
				      unsigned int dataoff,
				      const char **dptr, unsigned int *datalen,
				      struct nf_conntrack_expect *exp,
				      unsigned int matchoff,
				      unsigned int matchlen)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	struct nf_ct_sip_master *ct_sip_info = nfct_help_data(ct);
	union nf_inet_addr newaddr;
	u_int16_t port;
	__be16 srcport;
	char buffer[INET6_ADDRSTRLEN + sizeof("[]:nnnnn")];
	unsigned int buflen;

	/* Connection will come from reply */
	if (nf_inet_addr_cmp(&ct->tuplehash[dir].tuple.src.u3,
			     &ct->tuplehash[!dir].tuple.dst.u3))
		newaddr = exp->tuple.dst.u3;
	else
		newaddr = ct->tuplehash[!dir].tuple.dst.u3;

	/* If the signalling port matches the connection's source port in the
	 * original direction, try to use the destination port in the opposite
	 * direction. */
	srcport = ct_sip_info->forced_dport ? :
		  ct->tuplehash[dir].tuple.src.u.udp.port;
	if (exp->tuple.dst.u.udp.port == srcport)
		port = ntohs(ct->tuplehash[!dir].tuple.dst.u.udp.port);
	else
		port = ntohs(exp->tuple.dst.u.udp.port);

	exp->saved_addr = exp->tuple.dst.u3;
	exp->tuple.dst.u3 = newaddr;
	exp->saved_proto.udp.port = exp->tuple.dst.u.udp.port;
	exp->dir = !dir;
	exp->expectfn = nf_nat_sip_expected;

	for (; port != 0; port++) {
		int ret;

		exp->tuple.dst.u.udp.port = htons(port);
		ret = nf_ct_expect_related(exp, NF_CT_EXP_F_SKIP_MASTER);
		if (ret == 0)
			break;
		else if (ret != -EBUSY) {
			port = 0;
			break;
		}
	}

	if (port == 0) {
		nf_ct_helper_log(skb, ct, "all ports in use for SIP");
		return NF_DROP;
	}

	if (!nf_inet_addr_cmp(&exp->tuple.dst.u3, &exp->saved_addr) ||
	    exp->tuple.dst.u.udp.port != exp->saved_proto.udp.port) {
		buflen = sip_sprintf_addr_port(ct, buffer, &newaddr, port);
		if (!mangle_packet(skb, protoff, dataoff, dptr, datalen,
				   matchoff, matchlen, buffer, buflen)) {
			nf_ct_helper_log(skb, ct, "cannot mangle packet");
			goto err;
		}
	}
	return NF_ACCEPT;

err:
	nf_ct_unexpect_related(exp);
	return NF_DROP;
}

static int mangle_content_len(struct sk_buff *skb, unsigned int protoff,
			      unsigned int dataoff,
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
	return mangle_packet(skb, protoff, dataoff, dptr, datalen,
			     matchoff, matchlen, buffer, buflen);
}

static int mangle_sdp_packet(struct sk_buff *skb, unsigned int protoff,
			     unsigned int dataoff,
			     const char **dptr, unsigned int *datalen,
			     unsigned int sdpoff,
			     enum sdp_header_types type,
			     enum sdp_header_types term,
			     char *buffer, int buflen)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);
	unsigned int matchlen, matchoff;

	if (ct_sip_get_sdp_header(ct, *dptr, sdpoff, *datalen, type, term,
				  &matchoff, &matchlen) <= 0)
		return -ENOENT;
	return mangle_packet(skb, protoff, dataoff, dptr, datalen,
			     matchoff, matchlen, buffer, buflen) ? 0 : -EINVAL;
}

static unsigned int nf_nat_sdp_addr(struct sk_buff *skb, unsigned int protoff,
				    unsigned int dataoff,
				    const char **dptr, unsigned int *datalen,
				    unsigned int sdpoff,
				    enum sdp_header_types type,
				    enum sdp_header_types term,
				    const union nf_inet_addr *addr)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);
	char buffer[INET6_ADDRSTRLEN];
	unsigned int buflen;

	buflen = sip_sprintf_addr(ct, buffer, addr, false);
	if (mangle_sdp_packet(skb, protoff, dataoff, dptr, datalen,
			      sdpoff, type, term, buffer, buflen))
		return 0;

	return mangle_content_len(skb, protoff, dataoff, dptr, datalen);
}

static unsigned int nf_nat_sdp_port(struct sk_buff *skb, unsigned int protoff,
				    unsigned int dataoff,
				    const char **dptr, unsigned int *datalen,
				    unsigned int matchoff,
				    unsigned int matchlen,
				    u_int16_t port)
{
	char buffer[sizeof("nnnnn")];
	unsigned int buflen;

	buflen = sprintf(buffer, "%u", port);
	if (!mangle_packet(skb, protoff, dataoff, dptr, datalen,
			   matchoff, matchlen, buffer, buflen))
		return 0;

	return mangle_content_len(skb, protoff, dataoff, dptr, datalen);
}

static unsigned int nf_nat_sdp_session(struct sk_buff *skb, unsigned int protoff,
				       unsigned int dataoff,
				       const char **dptr, unsigned int *datalen,
				       unsigned int sdpoff,
				       const union nf_inet_addr *addr)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);
	char buffer[INET6_ADDRSTRLEN];
	unsigned int buflen;

	/* Mangle session description owner and contact addresses */
	buflen = sip_sprintf_addr(ct, buffer, addr, false);
	if (mangle_sdp_packet(skb, protoff, dataoff, dptr, datalen, sdpoff,
			      SDP_HDR_OWNER, SDP_HDR_MEDIA, buffer, buflen))
		return 0;

	switch (mangle_sdp_packet(skb, protoff, dataoff, dptr, datalen, sdpoff,
				  SDP_HDR_CONNECTION, SDP_HDR_MEDIA,
				  buffer, buflen)) {
	case 0:
	/*
	 * RFC 2327:
	 *
	 * Session description
	 *
	 * c=* (connection information - not required if included in all media)
	 */
	case -ENOENT:
		break;
	default:
		return 0;
	}

	return mangle_content_len(skb, protoff, dataoff, dptr, datalen);
}

/* So, this packet has hit the connection tracking matching code.
   Mangle it, and change the expectation to match the new version. */
static unsigned int nf_nat_sdp_media(struct sk_buff *skb, unsigned int protoff,
				     unsigned int dataoff,
				     const char **dptr, unsigned int *datalen,
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
	if (nf_inet_addr_cmp(&ct->tuplehash[dir].tuple.src.u3,
			     &ct->tuplehash[!dir].tuple.dst.u3))
		*rtp_addr = rtp_exp->tuple.dst.u3;
	else
		*rtp_addr = ct->tuplehash[!dir].tuple.dst.u3;

	rtp_exp->saved_addr = rtp_exp->tuple.dst.u3;
	rtp_exp->tuple.dst.u3 = *rtp_addr;
	rtp_exp->saved_proto.udp.port = rtp_exp->tuple.dst.u.udp.port;
	rtp_exp->dir = !dir;
	rtp_exp->expectfn = nf_nat_sip_expected;

	rtcp_exp->saved_addr = rtcp_exp->tuple.dst.u3;
	rtcp_exp->tuple.dst.u3 = *rtp_addr;
	rtcp_exp->saved_proto.udp.port = rtcp_exp->tuple.dst.u.udp.port;
	rtcp_exp->dir = !dir;
	rtcp_exp->expectfn = nf_nat_sip_expected;

	/* Try to get same pair of ports: if not, try to change them. */
	for (port = ntohs(rtp_exp->tuple.dst.u.udp.port);
	     port != 0; port += 2) {
		int ret;

		rtp_exp->tuple.dst.u.udp.port = htons(port);
		ret = nf_ct_expect_related(rtp_exp,
					   NF_CT_EXP_F_SKIP_MASTER);
		if (ret == -EBUSY)
			continue;
		else if (ret < 0) {
			port = 0;
			break;
		}
		rtcp_exp->tuple.dst.u.udp.port = htons(port + 1);
		ret = nf_ct_expect_related(rtcp_exp,
					   NF_CT_EXP_F_SKIP_MASTER);
		if (ret == 0)
			break;
		else if (ret == -EBUSY) {
			nf_ct_unexpect_related(rtp_exp);
			continue;
		} else if (ret < 0) {
			nf_ct_unexpect_related(rtp_exp);
			port = 0;
			break;
		}
	}

	if (port == 0) {
		nf_ct_helper_log(skb, ct, "all ports in use for SDP media");
		goto err1;
	}

	/* Update media port. */
	if (rtp_exp->tuple.dst.u.udp.port != rtp_exp->saved_proto.udp.port &&
	    !nf_nat_sdp_port(skb, protoff, dataoff, dptr, datalen,
			     mediaoff, medialen, port)) {
		nf_ct_helper_log(skb, ct, "cannot mangle SDP message");
		goto err2;
	}

	return NF_ACCEPT;

err2:
	nf_ct_unexpect_related(rtp_exp);
	nf_ct_unexpect_related(rtcp_exp);
err1:
	return NF_DROP;
}

static struct nf_ct_helper_expectfn sip_nat = {
	.name		= "sip",
	.expectfn	= nf_nat_sip_expected,
};

static void __exit nf_nat_sip_fini(void)
{
	nf_nat_helper_unregister(&nat_helper_sip);
	RCU_INIT_POINTER(nf_nat_sip_hooks, NULL);
	nf_ct_helper_expectfn_unregister(&sip_nat);
	synchronize_rcu();
}

static const struct nf_nat_sip_hooks sip_hooks = {
	.msg		= nf_nat_sip,
	.seq_adjust	= nf_nat_sip_seq_adjust,
	.expect		= nf_nat_sip_expect,
	.sdp_addr	= nf_nat_sdp_addr,
	.sdp_port	= nf_nat_sdp_port,
	.sdp_session	= nf_nat_sdp_session,
	.sdp_media	= nf_nat_sdp_media,
};

static int __init nf_nat_sip_init(void)
{
	BUG_ON(nf_nat_sip_hooks != NULL);
	nf_nat_helper_register(&nat_helper_sip);
	RCU_INIT_POINTER(nf_nat_sip_hooks, &sip_hooks);
	nf_ct_helper_expectfn_register(&sip_nat);
	return 0;
}

module_init(nf_nat_sip_init);
module_exit(nf_nat_sip_fini);
