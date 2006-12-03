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

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_expect.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <linux/netfilter/nf_conntrack_sip.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian Hentschel <chentschel@arnet.com.ar>");
MODULE_DESCRIPTION("SIP connection tracking helper");
MODULE_ALIAS("ip_conntrack_sip");

#define MAX_PORTS	8
static unsigned short ports[MAX_PORTS];
static int ports_c;
module_param_array(ports, ushort, &ports_c, 0400);
MODULE_PARM_DESC(ports, "port numbers of SIP servers");

static unsigned int sip_timeout __read_mostly = SIP_TIMEOUT;
module_param(sip_timeout, uint, 0600);
MODULE_PARM_DESC(sip_timeout, "timeout for the master SIP session");

unsigned int (*nf_nat_sip_hook)(struct sk_buff **pskb,
				enum ip_conntrack_info ctinfo,
				struct nf_conn *ct,
				const char **dptr) __read_mostly;
EXPORT_SYMBOL_GPL(nf_nat_sip_hook);

unsigned int (*nf_nat_sdp_hook)(struct sk_buff **pskb,
				enum ip_conntrack_info ctinfo,
				struct nf_conntrack_expect *exp,
				const char *dptr) __read_mostly;
EXPORT_SYMBOL_GPL(nf_nat_sdp_hook);

static int digits_len(struct nf_conn *, const char *, const char *, int *);
static int epaddr_len(struct nf_conn *, const char *, const char *, int *);
static int skp_digits_len(struct nf_conn *, const char *, const char *, int *);
static int skp_epaddr_len(struct nf_conn *, const char *, const char *, int *);

struct sip_header_nfo {
	const char	*lname;
	const char	*sname;
	const char	*ln_str;
	size_t		lnlen;
	size_t		snlen;
	size_t		ln_strlen;
	int		case_sensitive;
	int		(*match_len)(struct nf_conn *, const char *,
				     const char *, int *);
};

static const struct sip_header_nfo ct_sip_hdrs[] = {
	[POS_REG_REQ_URI] = { 	/* SIP REGISTER request URI */
		.lname		= "sip:",
		.lnlen		= sizeof("sip:") - 1,
		.ln_str		= ":",
		.ln_strlen	= sizeof(":") - 1,
		.match_len	= epaddr_len,
	},
	[POS_REQ_URI] = { 	/* SIP request URI */
		.lname		= "sip:",
		.lnlen		= sizeof("sip:") - 1,
		.ln_str		= "@",
		.ln_strlen	= sizeof("@") - 1,
		.match_len	= epaddr_len,
	},
	[POS_FROM] = {		/* SIP From header */
		.lname		= "From:",
		.lnlen		= sizeof("From:") - 1,
		.sname		= "\r\nf:",
		.snlen		= sizeof("\r\nf:") - 1,
		.ln_str		= "sip:",
		.ln_strlen	= sizeof("sip:") - 1,
		.match_len	= skp_epaddr_len,
	},
	[POS_TO] = {		/* SIP To header */
		.lname		= "To:",
		.lnlen		= sizeof("To:") - 1,
		.sname		= "\r\nt:",
		.snlen		= sizeof("\r\nt:") - 1,
		.ln_str		= "sip:",
		.ln_strlen	= sizeof("sip:") - 1,
		.match_len	= skp_epaddr_len
	},
	[POS_VIA] = { 		/* SIP Via header */
		.lname		= "Via:",
		.lnlen		= sizeof("Via:") - 1,
		.sname		= "\r\nv:",
		.snlen		= sizeof("\r\nv:") - 1, /* rfc3261 "\r\n" */
		.ln_str		= "UDP ",
		.ln_strlen	= sizeof("UDP ") - 1,
		.match_len	= epaddr_len,
	},
	[POS_CONTACT] = { 	/* SIP Contact header */
		.lname		= "Contact:",
		.lnlen		= sizeof("Contact:") - 1,
		.sname		= "\r\nm:",
		.snlen		= sizeof("\r\nm:") - 1,
		.ln_str		= "sip:",
		.ln_strlen	= sizeof("sip:") - 1,
		.match_len	= skp_epaddr_len
	},
	[POS_CONTENT] = { 	/* SIP Content length header */
		.lname		= "Content-Length:",
		.lnlen		= sizeof("Content-Length:") - 1,
		.sname		= "\r\nl:",
		.snlen		= sizeof("\r\nl:") - 1,
		.ln_str		= ":",
		.ln_strlen	= sizeof(":") - 1,
		.match_len	= skp_digits_len
	},
	[POS_MEDIA] = {		/* SDP media info */
		.case_sensitive	= 1,
		.lname		= "\nm=",
		.lnlen		= sizeof("\nm=") - 1,
		.sname		= "\rm=",
		.snlen		= sizeof("\rm=") - 1,
		.ln_str		= "audio ",
		.ln_strlen	= sizeof("audio ") - 1,
		.match_len	= digits_len
	},
	[POS_OWNER_IP4] = {	/* SDP owner address*/
		.case_sensitive	= 1,
		.lname		= "\no=",
		.lnlen		= sizeof("\no=") - 1,
		.sname		= "\ro=",
		.snlen		= sizeof("\ro=") - 1,
		.ln_str		= "IN IP4 ",
		.ln_strlen	= sizeof("IN IP4 ") - 1,
		.match_len	= epaddr_len
	},
	[POS_CONNECTION_IP4] = {/* SDP connection info */
		.case_sensitive	= 1,
		.lname		= "\nc=",
		.lnlen		= sizeof("\nc=") - 1,
		.sname		= "\rc=",
		.snlen		= sizeof("\rc=") - 1,
		.ln_str		= "IN IP4 ",
		.ln_strlen	= sizeof("IN IP4 ") - 1,
		.match_len	= epaddr_len
	},
	[POS_OWNER_IP6] = {	/* SDP owner address*/
		.case_sensitive	= 1,
		.lname		= "\no=",
		.lnlen		= sizeof("\no=") - 1,
		.sname		= "\ro=",
		.snlen		= sizeof("\ro=") - 1,
		.ln_str		= "IN IP6 ",
		.ln_strlen	= sizeof("IN IP6 ") - 1,
		.match_len	= epaddr_len
	},
	[POS_CONNECTION_IP6] = {/* SDP connection info */
		.case_sensitive	= 1,
		.lname		= "\nc=",
		.lnlen		= sizeof("\nc=") - 1,
		.sname		= "\rc=",
		.snlen		= sizeof("\rc=") - 1,
		.ln_str		= "IN IP6 ",
		.ln_strlen	= sizeof("IN IP6 ") - 1,
		.match_len	= epaddr_len
	},
	[POS_SDP_HEADER] = { 	/* SDP version header */
		.case_sensitive	= 1,
		.lname		= "\nv=",
		.lnlen		= sizeof("\nv=") - 1,
		.sname		= "\rv=",
		.snlen		= sizeof("\rv=") - 1,
		.ln_str		= "=",
		.ln_strlen	= sizeof("=") - 1,
		.match_len	= digits_len
	}
};

/* get line lenght until first CR or LF seen. */
int ct_sip_lnlen(const char *line, const char *limit)
{
	const char *k = line;

	while ((line <= limit) && (*line == '\r' || *line == '\n'))
		line++;

	while (line <= limit) {
		if (*line == '\r' || *line == '\n')
			break;
		line++;
	}
	return line - k;
}
EXPORT_SYMBOL_GPL(ct_sip_lnlen);

/* Linear string search, case sensitive. */
const char *ct_sip_search(const char *needle, const char *haystack,
			  size_t needle_len, size_t haystack_len,
			  int case_sensitive)
{
	const char *limit = haystack + (haystack_len - needle_len);

	while (haystack <= limit) {
		if (case_sensitive) {
			if (strncmp(haystack, needle, needle_len) == 0)
				return haystack;
		} else {
			if (strnicmp(haystack, needle, needle_len) == 0)
				return haystack;
		}
		haystack++;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(ct_sip_search);

static int digits_len(struct nf_conn *ct, const char *dptr,
		      const char *limit, int *shift)
{
	int len = 0;
	while (dptr <= limit && isdigit(*dptr)) {
		dptr++;
		len++;
	}
	return len;
}

/* get digits lenght, skiping blank spaces. */
static int skp_digits_len(struct nf_conn *ct, const char *dptr,
			  const char *limit, int *shift)
{
	for (; dptr <= limit && *dptr == ' '; dptr++)
		(*shift)++;

	return digits_len(ct, dptr, limit, shift);
}

static int parse_addr(struct nf_conn *ct, const char *cp, const char **endp,
		      union nf_conntrack_address *addr, const char *limit)
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
static int epaddr_len(struct nf_conn *ct, const char *dptr,
		      const char *limit, int *shift)
{
	union nf_conntrack_address addr;
	const char *aux = dptr;

	if (!parse_addr(ct, dptr, &dptr, &addr, limit)) {
		DEBUGP("ip: %s parse failed.!\n", dptr);
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
static int skp_epaddr_len(struct nf_conn *ct, const char *dptr,
			  const char *limit, int *shift)
{
	int s = *shift;

	for (; dptr <= limit && *dptr != '@'; dptr++)
		(*shift)++;

	if (*dptr == '@') {
		dptr++;
		(*shift)++;
	} else
		*shift = s;

	return epaddr_len(ct, dptr, limit, shift);
}

/* Returns 0 if not found, -1 error parsing. */
int ct_sip_get_info(struct nf_conn *ct,
		    const char *dptr, size_t dlen,
		    unsigned int *matchoff,
		    unsigned int *matchlen,
		    enum sip_header_pos pos)
{
	const struct sip_header_nfo *hnfo = &ct_sip_hdrs[pos];
	const char *limit, *aux, *k = dptr;
	int shift = 0;

	limit = dptr + (dlen - hnfo->lnlen);

	while (dptr <= limit) {
		if ((strncmp(dptr, hnfo->lname, hnfo->lnlen) != 0) &&
		    (strncmp(dptr, hnfo->sname, hnfo->snlen) != 0)) {
			dptr++;
			continue;
		}
		aux = ct_sip_search(hnfo->ln_str, dptr, hnfo->ln_strlen,
		                    ct_sip_lnlen(dptr, limit),
				    hnfo->case_sensitive);
		if (!aux) {
			DEBUGP("'%s' not found in '%s'.\n", hnfo->ln_str,
			       hnfo->lname);
			return -1;
		}
		aux += hnfo->ln_strlen;

		*matchlen = hnfo->match_len(ct, aux, limit, &shift);
		if (!*matchlen)
			return -1;

		*matchoff = (aux - k) + shift;

		DEBUGP("%s match succeeded! - len: %u\n", hnfo->lname,
		       *matchlen);
		return 1;
	}
	DEBUGP("%s header not found.\n", hnfo->lname);
	return 0;
}
EXPORT_SYMBOL_GPL(ct_sip_get_info);

static int set_expected_rtp(struct sk_buff **pskb,
			    struct nf_conn *ct,
			    enum ip_conntrack_info ctinfo,
			    union nf_conntrack_address *addr,
			    __be16 port,
			    const char *dptr)
{
	struct nf_conntrack_expect *exp;
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	int family = ct->tuplehash[!dir].tuple.src.l3num;
	int ret;
	typeof(nf_nat_sdp_hook) nf_nat_sdp;

	exp = nf_conntrack_expect_alloc(ct);
	if (exp == NULL)
		return NF_DROP;
	nf_conntrack_expect_init(exp, family,
				 &ct->tuplehash[!dir].tuple.src.u3, addr,
				 IPPROTO_UDP, NULL, &port);

	nf_nat_sdp = rcu_dereference(nf_nat_sdp_hook);
	if (nf_nat_sdp && ct->status & IPS_NAT_MASK)
		ret = nf_nat_sdp(pskb, ctinfo, exp, dptr);
	else {
		if (nf_conntrack_expect_related(exp) != 0)
			ret = NF_DROP;
		else
			ret = NF_ACCEPT;
	}
	nf_conntrack_expect_put(exp);

	return ret;
}

static int sip_help(struct sk_buff **pskb,
		    unsigned int protoff,
		    struct nf_conn *ct,
		    enum ip_conntrack_info ctinfo)
{
	int family = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.l3num;
	union nf_conntrack_address addr;
	unsigned int dataoff, datalen;
	const char *dptr;
	int ret = NF_ACCEPT;
	int matchoff, matchlen;
	u_int16_t port;
	enum sip_header_pos pos;
	typeof(nf_nat_sip_hook) nf_nat_sip;

	/* No Data ? */
	dataoff = protoff + sizeof(struct udphdr);
	if (dataoff >= (*pskb)->len)
		return NF_ACCEPT;

	nf_ct_refresh(ct, *pskb, sip_timeout * HZ);

	if (!skb_is_nonlinear(*pskb))
		dptr = (*pskb)->data + dataoff;
	else {
		DEBUGP("Copy of skbuff not supported yet.\n");
		goto out;
	}

	nf_nat_sip = rcu_dereference(nf_nat_sip_hook);
	if (nf_nat_sip && ct->status & IPS_NAT_MASK) {
		if (!nf_nat_sip(pskb, ctinfo, ct, &dptr)) {
			ret = NF_DROP;
			goto out;
		}
	}

	datalen = (*pskb)->len - dataoff;
	if (datalen < sizeof("SIP/2.0 200") - 1)
		goto out;

	/* RTP info only in some SDP pkts */
	if (memcmp(dptr, "INVITE", sizeof("INVITE") - 1) != 0 &&
	    memcmp(dptr, "SIP/2.0 200", sizeof("SIP/2.0 200") - 1) != 0) {
		goto out;
	}
	/* Get address and port from SDP packet. */
	pos = family == AF_INET ? POS_CONNECTION_IP4 : POS_CONNECTION_IP6;
	if (ct_sip_get_info(ct, dptr, datalen, &matchoff, &matchlen, pos) > 0) {

		/* We'll drop only if there are parse problems. */
		if (!parse_addr(ct, dptr + matchoff, NULL, &addr,
			        dptr + datalen)) {
			ret = NF_DROP;
			goto out;
		}
		if (ct_sip_get_info(ct, dptr, datalen, &matchoff, &matchlen,
		                    POS_MEDIA) > 0) {

			port = simple_strtoul(dptr + matchoff, NULL, 10);
			if (port < 1024) {
				ret = NF_DROP;
				goto out;
			}
			ret = set_expected_rtp(pskb, ct, ctinfo, &addr,
					       htons(port), dptr);
		}
	}
out:
	return ret;
}

static struct nf_conntrack_helper sip[MAX_PORTS][2] __read_mostly;
static char sip_names[MAX_PORTS][2][sizeof("sip-65535")] __read_mostly;

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
			sip[i][j].mask.src.l3num = 0xFFFF;
			sip[i][j].mask.src.u.udp.port = htons(0xFFFF);
			sip[i][j].mask.dst.protonum = 0xFF;
			sip[i][j].max_expected = 2;
			sip[i][j].timeout = 3 * 60; /* 3 minutes */
			sip[i][j].me = THIS_MODULE;
			sip[i][j].help = sip_help;

			tmpname = &sip_names[i][j][0];
			if (ports[i] == SIP_PORT)
				sprintf(tmpname, "sip");
			else
				sprintf(tmpname, "sip-%u", i);
			sip[i][j].name = tmpname;

			DEBUGP("port #%u: %u\n", i, ports[i]);

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
