/* SIP extension for IP connection tracking.
 *
 * (C) 2005 by Christian Hentschel <chentschel@arnet.com.ar>
 * based on RR's ip_conntrack_ftp.c and other modules.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/skbuff.h>
#include <linux/inet.h>
#include <linux/in.h>
#include <linux/udp.h>
#include <linux/netfilter.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_expect.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <linux/netfilter/nf_conntrack_sip.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian Hentschel <chentschel@arnet.com.ar>");
MODULE_DESCRIPTION("SIP connection tracking helper");
MODULE_ALIAS("ip_conntrack_sip");

#define MAX_PORTS	8
static unsigned short ports[MAX_PORTS];
static unsigned int ports_c;
module_param_array(ports, ushort, &ports_c, 0400);
MODULE_PARM_DESC(ports, "port numbers of SIP servers");

static unsigned int sip_timeout __read_mostly = SIP_TIMEOUT;
module_param(sip_timeout, uint, 0600);
MODULE_PARM_DESC(sip_timeout, "timeout for the master SIP session");

unsigned int (*nf_nat_sip_hook)(struct sk_buff *skb,
				const char **dptr,
				unsigned int *datalen) __read_mostly;
EXPORT_SYMBOL_GPL(nf_nat_sip_hook);

unsigned int (*nf_nat_sdp_hook)(struct sk_buff *skb,
				const char **dptr,
				unsigned int *datalen,
				struct nf_conntrack_expect *exp) __read_mostly;
EXPORT_SYMBOL_GPL(nf_nat_sdp_hook);

static int string_len(const struct nf_conn *ct, const char *dptr,
		      const char *limit, int *shift)
{
	int len = 0;

	while (dptr < limit && isalpha(*dptr)) {
		dptr++;
		len++;
	}
	return len;
}

static int digits_len(const struct nf_conn *ct, const char *dptr,
		      const char *limit, int *shift)
{
	int len = 0;
	while (dptr < limit && isdigit(*dptr)) {
		dptr++;
		len++;
	}
	return len;
}

static int parse_addr(const struct nf_conn *ct, const char *cp,
                      const char **endp, union nf_inet_addr *addr,
                      const char *limit)
{
	const char *end;
	int family = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.l3num;
	int ret = 0;

	switch (family) {
	case AF_INET:
		ret = in4_pton(cp, limit - cp, (u8 *)&addr->ip, -1, &end);
		break;
	case AF_INET6:
		ret = in6_pton(cp, limit - cp, (u8 *)&addr->ip6, -1, &end);
		break;
	default:
		BUG();
	}

	if (ret == 0 || end == cp)
		return 0;
	if (endp)
		*endp = end;
	return 1;
}

/* skip ip address. returns its length. */
static int epaddr_len(const struct nf_conn *ct, const char *dptr,
		      const char *limit, int *shift)
{
	union nf_inet_addr addr;
	const char *aux = dptr;

	if (!parse_addr(ct, dptr, &dptr, &addr, limit)) {
		pr_debug("ip: %s parse failed.!\n", dptr);
		return 0;
	}

	/* Port number */
	if (*dptr == ':') {
		dptr++;
		dptr += digits_len(ct, dptr, limit, shift);
	}
	return dptr - aux;
}

/* get address length, skiping user info. */
static int skp_epaddr_len(const struct nf_conn *ct, const char *dptr,
			  const char *limit, int *shift)
{
	const char *start = dptr;
	int s = *shift;

	/* Search for @, but stop at the end of the line.
	 * We are inside a sip: URI, so we don't need to worry about
	 * continuation lines. */
	while (dptr < limit &&
	       *dptr != '@' && *dptr != '\r' && *dptr != '\n') {
		(*shift)++;
		dptr++;
	}

	if (dptr < limit && *dptr == '@') {
		dptr++;
		(*shift)++;
	} else {
		dptr = start;
		*shift = s;
	}

	return epaddr_len(ct, dptr, limit, shift);
}

/* Parse a SIP request line of the form:
 *
 * Request-Line = Method SP Request-URI SP SIP-Version CRLF
 *
 * and return the offset and length of the address contained in the Request-URI.
 */
int ct_sip_parse_request(const struct nf_conn *ct,
			 const char *dptr, unsigned int datalen,
			 unsigned int *matchoff, unsigned int *matchlen)
{
	const char *start = dptr, *limit = dptr + datalen;
	unsigned int mlen;
	int shift = 0;

	/* Skip method and following whitespace */
	mlen = string_len(ct, dptr, limit, NULL);
	if (!mlen)
		return 0;
	dptr += mlen;
	if (++dptr >= limit)
		return 0;

	/* Find SIP URI */
	limit -= strlen("sip:");
	for (; dptr < limit; dptr++) {
		if (*dptr == '\r' || *dptr == '\n')
			return -1;
		if (strnicmp(dptr, "sip:", strlen("sip:")) == 0)
			break;
	}
	*matchlen = skp_epaddr_len(ct, dptr, limit, &shift);
	if (!*matchlen)
		return 0;
	*matchoff = dptr - start + shift;
	return 1;
}
EXPORT_SYMBOL_GPL(ct_sip_parse_request);

/* SIP header parsing: SIP headers are located at the beginning of a line, but
 * may span several lines, in which case the continuation lines begin with a
 * whitespace character. RFC 2543 allows lines to be terminated with CR, LF or
 * CRLF, RFC 3261 allows only CRLF, we support both.
 *
 * Headers are followed by (optionally) whitespace, a colon, again (optionally)
 * whitespace and the values. Whitespace in this context means any amount of
 * tabs, spaces and continuation lines, which are treated as a single whitespace
 * character.
 *
 * Some headers may appear multiple times. A comma seperated list of values is
 * equivalent to multiple headers.
 */
static const struct sip_header ct_sip_hdrs[] = {
	[SIP_HDR_FROM]			= SIP_HDR("From", "f", "sip:", skp_epaddr_len),
	[SIP_HDR_TO]			= SIP_HDR("To", "t", "sip:", skp_epaddr_len),
	[SIP_HDR_CONTACT]		= SIP_HDR("Contact", "m", "sip:", skp_epaddr_len),
	[SIP_HDR_VIA]			= SIP_HDR("Via", "v", "UDP ", epaddr_len),
	[SIP_HDR_CONTENT_LENGTH]	= SIP_HDR("Content-Length", "l", NULL, digits_len),
};

static const char *sip_follow_continuation(const char *dptr, const char *limit)
{
	/* Walk past newline */
	if (++dptr >= limit)
		return NULL;

	/* Skip '\n' in CR LF */
	if (*(dptr - 1) == '\r' && *dptr == '\n') {
		if (++dptr >= limit)
			return NULL;
	}

	/* Continuation line? */
	if (*dptr != ' ' && *dptr != '\t')
		return NULL;

	/* skip leading whitespace */
	for (; dptr < limit; dptr++) {
		if (*dptr != ' ' && *dptr != '\t')
			break;
	}
	return dptr;
}

static const char *sip_skip_whitespace(const char *dptr, const char *limit)
{
	for (; dptr < limit; dptr++) {
		if (*dptr == ' ')
			continue;
		if (*dptr != '\r' && *dptr != '\n')
			break;
		dptr = sip_follow_continuation(dptr, limit);
		if (dptr == NULL)
			return NULL;
	}
	return dptr;
}

/* Search within a SIP header value, dealing with continuation lines */
static const char *ct_sip_header_search(const char *dptr, const char *limit,
					const char *needle, unsigned int len)
{
	for (limit -= len; dptr < limit; dptr++) {
		if (*dptr == '\r' || *dptr == '\n') {
			dptr = sip_follow_continuation(dptr, limit);
			if (dptr == NULL)
				break;
			continue;
		}

		if (strnicmp(dptr, needle, len) == 0)
			return dptr;
	}
	return NULL;
}

int ct_sip_get_header(const struct nf_conn *ct, const char *dptr,
		      unsigned int dataoff, unsigned int datalen,
		      enum sip_header_types type,
		      unsigned int *matchoff, unsigned int *matchlen)
{
	const struct sip_header *hdr = &ct_sip_hdrs[type];
	const char *start = dptr, *limit = dptr + datalen;
	int shift = 0;

	for (dptr += dataoff; dptr < limit; dptr++) {
		/* Find beginning of line */
		if (*dptr != '\r' && *dptr != '\n')
			continue;
		if (++dptr >= limit)
			break;
		if (*(dptr - 1) == '\r' && *dptr == '\n') {
			if (++dptr >= limit)
				break;
		}

		/* Skip continuation lines */
		if (*dptr == ' ' || *dptr == '\t')
			continue;

		/* Find header. Compact headers must be followed by a
		 * non-alphabetic character to avoid mismatches. */
		if (limit - dptr >= hdr->len &&
		    strnicmp(dptr, hdr->name, hdr->len) == 0)
			dptr += hdr->len;
		else if (hdr->cname && limit - dptr >= hdr->clen + 1 &&
			 strnicmp(dptr, hdr->cname, hdr->clen) == 0 &&
			 !isalpha(*(dptr + hdr->clen + 1)))
			dptr += hdr->clen;
		else
			continue;

		/* Find and skip colon */
		dptr = sip_skip_whitespace(dptr, limit);
		if (dptr == NULL)
			break;
		if (*dptr != ':' || ++dptr >= limit)
			break;

		/* Skip whitespace after colon */
		dptr = sip_skip_whitespace(dptr, limit);
		if (dptr == NULL)
			break;

		*matchoff = dptr - start;
		if (hdr->search) {
			dptr = ct_sip_header_search(dptr, limit, hdr->search,
						    hdr->slen);
			if (!dptr)
				return -1;
			dptr += hdr->slen;
		}

		*matchlen = hdr->match_len(ct, dptr, limit, &shift);
		if (!*matchlen)
			return -1;
		*matchoff = dptr - start + shift;
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(ct_sip_get_header);

/* Get next header field in a list of comma seperated values */
static int ct_sip_next_header(const struct nf_conn *ct, const char *dptr,
			      unsigned int dataoff, unsigned int datalen,
			      enum sip_header_types type,
			      unsigned int *matchoff, unsigned int *matchlen)
{
	const struct sip_header *hdr = &ct_sip_hdrs[type];
	const char *start = dptr, *limit = dptr + datalen;
	int shift = 0;

	dptr += dataoff;

	dptr = ct_sip_header_search(dptr, limit, ",", strlen(","));
	if (!dptr)
		return 0;

	dptr = ct_sip_header_search(dptr, limit, hdr->search, hdr->slen);
	if (!dptr)
		return 0;
	dptr += hdr->slen;

	*matchoff = dptr - start;
	*matchlen = hdr->match_len(ct, dptr, limit, &shift);
	if (!*matchlen)
		return -1;
	*matchoff += shift;
	return 1;
}

/* Walk through headers until a parsable one is found or no header of the
 * given type is left. */
static int ct_sip_walk_headers(const struct nf_conn *ct, const char *dptr,
			       unsigned int dataoff, unsigned int datalen,
			       enum sip_header_types type, int *in_header,
			       unsigned int *matchoff, unsigned int *matchlen)
{
	int ret;

	if (in_header && *in_header) {
		while (1) {
			ret = ct_sip_next_header(ct, dptr, dataoff, datalen,
						 type, matchoff, matchlen);
			if (ret > 0)
				return ret;
			if (ret == 0)
				break;
			dataoff += *matchoff;
		}
		*in_header = 0;
	}

	while (1) {
		ret = ct_sip_get_header(ct, dptr, dataoff, datalen,
					type, matchoff, matchlen);
		if (ret > 0)
			break;
		if (ret == 0)
			return ret;
		dataoff += *matchoff;
	}

	if (in_header)
		*in_header = 1;
	return 1;
}

/* Locate a SIP header, parse the URI and return the offset and length of
 * the address as well as the address and port themselves. A stream of
 * headers can be parsed by handing in a non-NULL datalen and in_header
 * pointer.
 */
int ct_sip_parse_header_uri(const struct nf_conn *ct, const char *dptr,
			    unsigned int *dataoff, unsigned int datalen,
			    enum sip_header_types type, int *in_header,
			    unsigned int *matchoff, unsigned int *matchlen,
			    union nf_inet_addr *addr, __be16 *port)
{
	const char *c, *limit = dptr + datalen;
	unsigned int p;
	int ret;

	ret = ct_sip_walk_headers(ct, dptr, dataoff ? *dataoff : 0, datalen,
				  type, in_header, matchoff, matchlen);
	WARN_ON(ret < 0);
	if (ret == 0)
		return ret;

	if (!parse_addr(ct, dptr + *matchoff, &c, addr, limit))
		return -1;
	if (*c == ':') {
		c++;
		p = simple_strtoul(c, (char **)&c, 10);
		if (p < 1024 || p > 65535)
			return -1;
		*port = htons(p);
	} else
		*port = htons(SIP_PORT);

	if (dataoff)
		*dataoff = c - dptr;
	return 1;
}
EXPORT_SYMBOL_GPL(ct_sip_parse_header_uri);

/* SDP header parsing: a SDP session description contains an ordered set of
 * headers, starting with a section containing general session parameters,
 * optionally followed by multiple media descriptions.
 *
 * SDP headers always start at the beginning of a line. According to RFC 2327:
 * "The sequence CRLF (0x0d0a) is used to end a record, although parsers should
 * be tolerant and also accept records terminated with a single newline
 * character". We handle both cases.
 */
static const struct sip_header ct_sdp_hdrs[] = {
	[SDP_HDR_VERSION]		= SDP_HDR("v=", NULL, digits_len),
	[SDP_HDR_OWNER_IP4]		= SDP_HDR("o=", "IN IP4 ", epaddr_len),
	[SDP_HDR_CONNECTION_IP4]	= SDP_HDR("c=", "IN IP4 ", epaddr_len),
	[SDP_HDR_OWNER_IP6]		= SDP_HDR("o=", "IN IP6 ", epaddr_len),
	[SDP_HDR_CONNECTION_IP6]	= SDP_HDR("c=", "IN IP6 ", epaddr_len),
	[SDP_HDR_MEDIA]			= SDP_HDR("m=", "audio ", digits_len),
};

/* Linear string search within SDP header values */
static const char *ct_sdp_header_search(const char *dptr, const char *limit,
					const char *needle, unsigned int len)
{
	for (limit -= len; dptr < limit; dptr++) {
		if (*dptr == '\r' || *dptr == '\n')
			break;
		if (strncmp(dptr, needle, len) == 0)
			return dptr;
	}
	return NULL;
}

/* Locate a SDP header (optionally a substring within the header value),
 * optionally stopping at the first occurence of the term header, parse
 * it and return the offset and length of the data we're interested in.
 */
int ct_sip_get_sdp_header(const struct nf_conn *ct, const char *dptr,
			  unsigned int dataoff, unsigned int datalen,
			  enum sdp_header_types type,
			  enum sdp_header_types term,
			  unsigned int *matchoff, unsigned int *matchlen)
{
	const struct sip_header *hdr = &ct_sdp_hdrs[type];
	const struct sip_header *thdr = &ct_sdp_hdrs[term];
	const char *start = dptr, *limit = dptr + datalen;
	int shift = 0;

	for (dptr += dataoff; dptr < limit; dptr++) {
		/* Find beginning of line */
		if (*dptr != '\r' && *dptr != '\n')
			continue;
		if (++dptr >= limit)
			break;
		if (*(dptr - 1) == '\r' && *dptr == '\n') {
			if (++dptr >= limit)
				break;
		}

		if (term != SDP_HDR_UNSPEC &&
		    limit - dptr >= thdr->len &&
		    strnicmp(dptr, thdr->name, thdr->len) == 0)
			break;
		else if (limit - dptr >= hdr->len &&
			 strnicmp(dptr, hdr->name, hdr->len) == 0)
			dptr += hdr->len;
		else
			continue;

		*matchoff = dptr - start;
		if (hdr->search) {
			dptr = ct_sdp_header_search(dptr, limit, hdr->search,
						    hdr->slen);
			if (!dptr)
				return -1;
			dptr += hdr->slen;
		}

		*matchlen = hdr->match_len(ct, dptr, limit, &shift);
		if (!*matchlen)
			return -1;
		*matchoff = dptr - start + shift;
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(ct_sip_get_sdp_header);

static int set_expected_rtp(struct sk_buff *skb,
			    const char **dptr, unsigned int *datalen,
			    union nf_inet_addr *addr, __be16 port)
{
	struct nf_conntrack_expect *exp;
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	int family = ct->tuplehash[!dir].tuple.src.l3num;
	int ret;
	typeof(nf_nat_sdp_hook) nf_nat_sdp;

	exp = nf_ct_expect_alloc(ct);
	if (exp == NULL)
		return NF_DROP;
	nf_ct_expect_init(exp, NF_CT_EXPECT_CLASS_DEFAULT, family,
			  &ct->tuplehash[!dir].tuple.src.u3, addr,
			  IPPROTO_UDP, NULL, &port);

	nf_nat_sdp = rcu_dereference(nf_nat_sdp_hook);
	if (nf_nat_sdp && ct->status & IPS_NAT_MASK)
		ret = nf_nat_sdp(skb, dptr, datalen, exp);
	else {
		if (nf_ct_expect_related(exp) != 0)
			ret = NF_DROP;
		else
			ret = NF_ACCEPT;
	}
	nf_ct_expect_put(exp);

	return ret;
}

static int sip_help(struct sk_buff *skb,
		    unsigned int protoff,
		    struct nf_conn *ct,
		    enum ip_conntrack_info ctinfo)
{
	int family = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.l3num;
	union nf_inet_addr addr;
	unsigned int dataoff, datalen;
	const char *dptr;
	int ret = NF_ACCEPT;
	unsigned int matchoff, matchlen;
	u_int16_t port;
	enum sdp_header_types type;
	typeof(nf_nat_sip_hook) nf_nat_sip;

	/* No Data ? */
	dataoff = protoff + sizeof(struct udphdr);
	if (dataoff >= skb->len)
		return NF_ACCEPT;

	nf_ct_refresh(ct, skb, sip_timeout * HZ);

	if (!skb_is_nonlinear(skb))
		dptr = skb->data + dataoff;
	else {
		pr_debug("Copy of skbuff not supported yet.\n");
		goto out;
	}

	nf_nat_sip = rcu_dereference(nf_nat_sip_hook);
	if (nf_nat_sip && ct->status & IPS_NAT_MASK) {
		if (!nf_nat_sip(skb, &dptr, &datalen)) {
			ret = NF_DROP;
			goto out;
		}
	}

	datalen = skb->len - dataoff;
	if (datalen < strlen("SIP/2.0 200"))
		goto out;

	/* RTP info only in some SDP pkts */
	if (strnicmp(dptr, "INVITE", strlen("INVITE")) != 0 &&
	    strnicmp(dptr, "UPDATE", strlen("UPDATE")) != 0 &&
	    strnicmp(dptr, "SIP/2.0 180", strlen("SIP/2.0 180")) != 0 &&
	    strnicmp(dptr, "SIP/2.0 183", strlen("SIP/2.0 183")) != 0 &&
	    strnicmp(dptr, "SIP/2.0 200", strlen("SIP/2.0 200")) != 0) {
		goto out;
	}
	/* Get address and port from SDP packet. */
	type = family == AF_INET ? SDP_HDR_CONNECTION_IP4 :
				   SDP_HDR_CONNECTION_IP6;
	if (ct_sip_get_sdp_header(ct, dptr, 0, datalen, type, SDP_HDR_UNSPEC,
				  &matchoff, &matchlen) > 0) {

		/* We'll drop only if there are parse problems. */
		if (!parse_addr(ct, dptr + matchoff, NULL, &addr,
				dptr + datalen)) {
			ret = NF_DROP;
			goto out;
		}
		if (ct_sip_get_sdp_header(ct, dptr, 0, datalen,
					  SDP_HDR_MEDIA, SDP_HDR_UNSPEC,
					  &matchoff, &matchlen) > 0) {

			port = simple_strtoul(dptr + matchoff, NULL, 10);
			if (port < 1024) {
				ret = NF_DROP;
				goto out;
			}
			ret = set_expected_rtp(skb, &dptr, &datalen,
					       &addr, htons(port));
		}
	}
out:
	return ret;
}

static struct nf_conntrack_helper sip[MAX_PORTS][2] __read_mostly;
static char sip_names[MAX_PORTS][2][sizeof("sip-65535")] __read_mostly;

static const struct nf_conntrack_expect_policy sip_exp_policy = {
	.max_expected	= 2,
	.timeout	= 3 * 60,
};

static void nf_conntrack_sip_fini(void)
{
	int i, j;

	for (i = 0; i < ports_c; i++) {
		for (j = 0; j < 2; j++) {
			if (sip[i][j].me == NULL)
				continue;
			nf_conntrack_helper_unregister(&sip[i][j]);
		}
	}
}

static int __init nf_conntrack_sip_init(void)
{
	int i, j, ret;
	char *tmpname;

	if (ports_c == 0)
		ports[ports_c++] = SIP_PORT;

	for (i = 0; i < ports_c; i++) {
		memset(&sip[i], 0, sizeof(sip[i]));

		sip[i][0].tuple.src.l3num = AF_INET;
		sip[i][1].tuple.src.l3num = AF_INET6;
		for (j = 0; j < 2; j++) {
			sip[i][j].tuple.dst.protonum = IPPROTO_UDP;
			sip[i][j].tuple.src.u.udp.port = htons(ports[i]);
			sip[i][j].expect_policy = &sip_exp_policy;
			sip[i][j].me = THIS_MODULE;
			sip[i][j].help = sip_help;

			tmpname = &sip_names[i][j][0];
			if (ports[i] == SIP_PORT)
				sprintf(tmpname, "sip");
			else
				sprintf(tmpname, "sip-%u", i);
			sip[i][j].name = tmpname;

			pr_debug("port #%u: %u\n", i, ports[i]);

			ret = nf_conntrack_helper_register(&sip[i][j]);
			if (ret) {
				printk("nf_ct_sip: failed to register helper "
				       "for pf: %u port: %u\n",
				       sip[i][j].tuple.src.l3num, ports[i]);
				nf_conntrack_sip_fini();
				return ret;
			}
		}
	}
	return 0;
}

module_init(nf_conntrack_sip_init);
module_exit(nf_conntrack_sip_fini);
