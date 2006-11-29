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
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/udp.h>

#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv4/ip_conntrack_helper.h>
#include <linux/netfilter_ipv4/ip_conntrack_sip.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian Hentschel <chentschel@arnet.com.ar>");
MODULE_DESCRIPTION("SIP connection tracking helper");

#define MAX_PORTS	8
static unsigned short ports[MAX_PORTS];
static int ports_c;
module_param_array(ports, ushort, &ports_c, 0400);
MODULE_PARM_DESC(ports, "port numbers of sip servers");

static unsigned int sip_timeout = SIP_TIMEOUT;
module_param(sip_timeout, uint, 0600);
MODULE_PARM_DESC(sip_timeout, "timeout for the master SIP session");

unsigned int (*ip_nat_sip_hook)(struct sk_buff **pskb,
				enum ip_conntrack_info ctinfo,
				struct ip_conntrack *ct,
				const char **dptr);
EXPORT_SYMBOL_GPL(ip_nat_sip_hook);

unsigned int (*ip_nat_sdp_hook)(struct sk_buff **pskb,
				enum ip_conntrack_info ctinfo,
				struct ip_conntrack_expect *exp,
				const char *dptr);
EXPORT_SYMBOL_GPL(ip_nat_sdp_hook);

static int digits_len(const char *dptr, const char *limit, int *shift);
static int epaddr_len(const char *dptr, const char *limit, int *shift);
static int skp_digits_len(const char *dptr, const char *limit, int *shift);
static int skp_epaddr_len(const char *dptr, const char *limit, int *shift);

struct sip_header_nfo {
	const char	*lname;
	const char	*sname;
	const char	*ln_str;
	size_t		lnlen;
	size_t		snlen;
	size_t		ln_strlen;
	int		(*match_len)(const char *, const char *, int *);
};

static struct sip_header_nfo ct_sip_hdrs[] = {
	[POS_REQ_HEADER] = { 	/* SIP Requests headers */
		.lname		= "sip:",
		.lnlen		= sizeof("sip:") - 1,
		.sname		= "sip:",
		.snlen		= sizeof("sip:") - 1, /* yes, i know.. ;) */
		.ln_str		= "@",
		.ln_strlen	= sizeof("@") - 1,
		.match_len	= epaddr_len
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
		.lname		= "\nm=",
		.lnlen		= sizeof("\nm=") - 1,
		.sname		= "\rm=",
		.snlen		= sizeof("\rm=") - 1,
		.ln_str		= "audio ",
		.ln_strlen	= sizeof("audio ") - 1,
		.match_len	= digits_len
	},
	[POS_OWNER] = { 	/* SDP owner address*/
		.lname		= "\no=",
		.lnlen		= sizeof("\no=") - 1,
		.sname		= "\ro=",
		.snlen		= sizeof("\ro=") - 1,
		.ln_str		= "IN IP4 ",
		.ln_strlen	= sizeof("IN IP4 ") - 1,
		.match_len	= epaddr_len
	},
	[POS_CONNECTION] = { 	/* SDP connection info */
		.lname		= "\nc=",
		.lnlen		= sizeof("\nc=") - 1,
		.sname		= "\rc=",
		.snlen		= sizeof("\rc=") - 1,
		.ln_str		= "IN IP4 ",
		.ln_strlen	= sizeof("IN IP4 ") - 1,
		.match_len	= epaddr_len
	},
	[POS_SDP_HEADER] = { 	/* SDP version header */
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
                          size_t needle_len, size_t haystack_len)
{
	const char *limit = haystack + (haystack_len - needle_len);

	while (haystack <= limit) {
		if (memcmp(haystack, needle, needle_len) == 0)
			return haystack;
		haystack++;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(ct_sip_search);

static int digits_len(const char *dptr, const char *limit, int *shift)
{
	int len = 0;
	while (dptr <= limit && isdigit(*dptr)) {
		dptr++;
		len++;
	}
	return len;
}

/* get digits lenght, skiping blank spaces. */
static int skp_digits_len(const char *dptr, const char *limit, int *shift)
{
	for (; dptr <= limit && *dptr == ' '; dptr++)
		(*shift)++;

	return digits_len(dptr, limit, shift);
}

/* Simple ipaddr parser.. */
static int parse_ipaddr(const char *cp,	const char **endp,
			__be32 *ipaddr, const char *limit)
{
	unsigned long int val;
	int i, digit = 0;

	for (i = 0, *ipaddr = 0; cp <= limit && i < 4; i++) {
		digit = 0;
		if (!isdigit(*cp))
			break;

		val = simple_strtoul(cp, (char **)&cp, 10);
		if (val > 0xFF)
			return -1;

		((u_int8_t *)ipaddr)[i] = val;
		digit = 1;

		if (*cp != '.')
			break;
		cp++;
	}
	if (!digit)
		return -1;

	if (endp)
		*endp = cp;

	return 0;
}

/* skip ip address. returns it lenght. */
static int epaddr_len(const char *dptr, const char *limit, int *shift)
{
	const char *aux = dptr;
	__be32 ip;

	if (parse_ipaddr(dptr, &dptr, &ip, limit) < 0) {
		DEBUGP("ip: %s parse failed.!\n", dptr);
		return 0;
	}

	/* Port number */
	if (*dptr == ':') {
		dptr++;
		dptr += digits_len(dptr, limit, shift);
	}
	return dptr - aux;
}

/* get address length, skiping user info. */
static int skp_epaddr_len(const char *dptr, const char *limit, int *shift)
{
	int s = *shift;

	for (; dptr <= limit && *dptr != '@'; dptr++)
		(*shift)++;

	if (*dptr == '@') {
		dptr++;
		(*shift)++;
	} else
		*shift = s;

	return epaddr_len(dptr, limit, shift);
}

/* Returns 0 if not found, -1 error parsing. */
int ct_sip_get_info(const char *dptr, size_t dlen,
		    unsigned int *matchoff,
		    unsigned int *matchlen,
		    enum sip_header_pos pos)
{
	struct sip_header_nfo *hnfo = &ct_sip_hdrs[pos];
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
		                    ct_sip_lnlen(dptr, limit));
		if (!aux) {
			DEBUGP("'%s' not found in '%s'.\n", hnfo->ln_str,
			       hnfo->lname);
			return -1;
		}
		aux += hnfo->ln_strlen;

		*matchlen = hnfo->match_len(aux, limit, &shift);
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
			    struct ip_conntrack *ct,
			    enum ip_conntrack_info ctinfo,
			    __be32 ipaddr, u_int16_t port,
			    const char *dptr)
{
	struct ip_conntrack_expect *exp;
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	int ret;
	typeof(ip_nat_sdp_hook) ip_nat_sdp;

	exp = ip_conntrack_expect_alloc(ct);
	if (exp == NULL)
		return NF_DROP;

	exp->tuple.src.ip = ct->tuplehash[!dir].tuple.src.ip;
	exp->tuple.src.u.udp.port = 0;
	exp->tuple.dst.ip = ipaddr;
	exp->tuple.dst.u.udp.port = htons(port);
	exp->tuple.dst.protonum = IPPROTO_UDP;

	exp->mask.src.ip = htonl(0xFFFFFFFF);
	exp->mask.src.u.udp.port = 0;
	exp->mask.dst.ip = htonl(0xFFFFFFFF);
	exp->mask.dst.u.udp.port = htons(0xFFFF);
	exp->mask.dst.protonum = 0xFF;

	exp->expectfn = NULL;
	exp->flags = 0;

	ip_nat_sdp = rcu_dereference(ip_nat_sdp_hook);
	if (ip_nat_sdp)
		ret = ip_nat_sdp(pskb, ctinfo, exp, dptr);
	else {
		if (ip_conntrack_expect_related(exp) != 0)
			ret = NF_DROP;
		else
			ret = NF_ACCEPT;
	}
	ip_conntrack_expect_put(exp);

	return ret;
}

static int sip_help(struct sk_buff **pskb,
		    struct ip_conntrack *ct,
		    enum ip_conntrack_info ctinfo)
{
	unsigned int dataoff, datalen;
	const char *dptr;
	int ret = NF_ACCEPT;
	int matchoff, matchlen;
	__be32 ipaddr;
	u_int16_t port;
	typeof(ip_nat_sip_hook) ip_nat_sip;

	/* No Data ? */
	dataoff = (*pskb)->nh.iph->ihl*4 + sizeof(struct udphdr);
	if (dataoff >= (*pskb)->len) {
		DEBUGP("skb->len = %u\n", (*pskb)->len);
		return NF_ACCEPT;
        }

	ip_ct_refresh(ct, *pskb, sip_timeout * HZ);

	if (!skb_is_nonlinear(*pskb))
		dptr = (*pskb)->data + dataoff;
	else {
		DEBUGP("Copy of skbuff not supported yet.\n");
		goto out;
	}

	ip_nat_sip = rcu_dereference(ip_nat_sip_hook);
	if (ip_nat_sip) {
		if (!ip_nat_sip(pskb, ctinfo, ct, &dptr)) {
			ret = NF_DROP;
			goto out;
		}
	}

	/* After this point NAT, could have mangled skb, so
	   we need to recalculate payload lenght. */
	datalen = (*pskb)->len - dataoff;

	if (datalen < (sizeof("SIP/2.0 200") - 1))
		goto out;

	/* RTP info only in some SDP pkts */
	if (memcmp(dptr, "INVITE", sizeof("INVITE") - 1) != 0 &&
	    memcmp(dptr, "SIP/2.0 200", sizeof("SIP/2.0 200") - 1) != 0) {
		goto out;
	}
	/* Get ip and port address from SDP packet. */
	if (ct_sip_get_info(dptr, datalen, &matchoff, &matchlen,
	                    POS_CONNECTION) > 0) {

		/* We'll drop only if there are parse problems. */
		if (parse_ipaddr(dptr + matchoff, NULL, &ipaddr,
		                 dptr + datalen) < 0) {
			ret = NF_DROP;
			goto out;
		}
		if (ct_sip_get_info(dptr, datalen, &matchoff, &matchlen,
		                    POS_MEDIA) > 0) {

			port = simple_strtoul(dptr + matchoff, NULL, 10);
			if (port < 1024) {
				ret = NF_DROP;
				goto out;
			}
			ret = set_expected_rtp(pskb, ct, ctinfo,
					       ipaddr, port, dptr);
		}
	}
out:
	return ret;
}

static struct ip_conntrack_helper sip[MAX_PORTS];
static char sip_names[MAX_PORTS][10];

static void fini(void)
{
	int i;
	for (i = 0; i < ports_c; i++) {
		DEBUGP("unregistering helper for port %d\n", ports[i]);
		ip_conntrack_helper_unregister(&sip[i]);
	}
}

static int __init init(void)
{
	int i, ret;
	char *tmpname;

	if (ports_c == 0)
		ports[ports_c++] = SIP_PORT;

	for (i = 0; i < ports_c; i++) {
		/* Create helper structure */
		memset(&sip[i], 0, sizeof(struct ip_conntrack_helper));

		sip[i].tuple.dst.protonum = IPPROTO_UDP;
		sip[i].tuple.src.u.udp.port = htons(ports[i]);
		sip[i].mask.src.u.udp.port = htons(0xFFFF);
		sip[i].mask.dst.protonum = 0xFF;
		sip[i].max_expected = 2;
		sip[i].timeout = 3 * 60; /* 3 minutes */
		sip[i].me = THIS_MODULE;
		sip[i].help = sip_help;

		tmpname = &sip_names[i][0];
		if (ports[i] == SIP_PORT)
			sprintf(tmpname, "sip");
		else
			sprintf(tmpname, "sip-%d", i);
		sip[i].name = tmpname;

		DEBUGP("port #%d: %d\n", i, ports[i]);

		ret = ip_conntrack_helper_register(&sip[i]);
		if (ret) {
			printk("ERROR registering helper for port %d\n",
				ports[i]);
			fini();
			return ret;
		}
	}
	return 0;
}

module_init(init);
module_exit(fini);
