/*	$OpenBSD: common.c,v 1.4 2018/12/15 15:16:12 eric Exp $	*/
/*
 * Copyright (c) 2012 Eric Faurot <eric@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>

#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <netdb.h>
#include <resolv.h>
#include <stdio.h>
#include <string.h>

#include "common.h"

int long_err;
int gai_errno;
int rrset_errno;


char *
gethostarg(char *n)
{
	if (n == NULL)
		return (n);
	if (!strcmp(n, "NULL"))
		return (NULL);
	if (!strcmp(n, "EMPTY"))
		return ("");
	return (n);
}

const char *rrsetstrerror(int);
char * print_addr(const struct sockaddr *, char *, size_t);

struct kv { int code; const char *name; };

struct kv kv_family[] = {
	{ AF_UNIX,	"unix" },
	{ AF_INET,	"inet" },
	{ AF_INET6,	"inet6" },
	{ 0,	NULL, }
};
struct kv kv_socktype[] = {
	{ SOCK_STREAM,		"stream" },
	{ SOCK_DGRAM,		"dgram" },
	{ SOCK_RAW,		"raw" },
	{ SOCK_SEQPACKET,	"seqpacket" },
	{ 0,	NULL, }
};
struct kv kv_protocol[] = {
	{ IPPROTO_UDP, "udp" },
	{ IPPROTO_TCP, "tcp" },
	{ IPPROTO_ICMP, "icmp" },
	{ IPPROTO_ICMPV6, "icmpv6" },
	{ 0,	NULL, }
};

static const char *
kv_lookup_name(struct kv *kv, int code, char *buf, size_t sz)
{
	while (kv->name) {
		if (kv->code == code)
			return (kv->name);
		kv++;
	}
	snprintf(buf, sz, "%i", code);
	return (buf);
}

struct keyval {
        const char *key;
        int	    value;   
};

static struct keyval kv_class[] = {
	{ "IN",	C_IN },
	{ "CHAOS", C_CHAOS },
	{ "HS", C_HS },
	{ "ANY", C_ANY },
	{ NULL, 	0 },
};

static struct keyval kv_type[] = {
	{ "A",		T_A	},
	{ "NS",		T_NS	},
	{ "MD",		T_MD	},
	{ "MF",		T_MF	},
	{ "CNAME",	T_CNAME	},
	{ "SOA",	T_SOA	},
	{ "MB",		T_MB	},
	{ "MG",		T_MG	},
	{ "MR",		T_MR	},
	{ "NULL",	T_NULL	},
	{ "WKS",	T_WKS	},
	{ "PTR",	T_PTR	},
	{ "HINFO",	T_HINFO	},
	{ "MINFO",	T_MINFO	},
	{ "MX",		T_MX	},
	{ "TXT",	T_TXT	},

	{ "AAAA",	T_AAAA	},

	{ "AXFR",	T_AXFR	},
	{ "MAILB",	T_MAILB	},
	{ "MAILA",	T_MAILA	},
	{ "ANY",	T_ANY	},
	{ NULL, 	0 },
};

static struct keyval kv_rcode[] = {
	{ "NOERROR",	NOERROR	},
	{ "FORMERR",	FORMERR },
	{ "SERVFAIL",	SERVFAIL },
	{ "NXDOMAIN",	NXDOMAIN },
	{ "NOTIMP",	NOTIMP },
	{ "REFUSED",	REFUSED },
	{ NULL, 	0 },
};

static struct keyval kv_resopt[] = {
	{ "DEBUG",	RES_DEBUG },
	{ "AAONLY",	RES_AAONLY },
	{ "USEVC",	RES_USEVC },
	{ "PRIMARY",	RES_PRIMARY },
	{ "IGNTC",	RES_IGNTC },
	{ "RECURSE",	RES_RECURSE },
	{ "DEFNAMES",	RES_DEFNAMES },
	{ "STAYOPEN",	RES_STAYOPEN },
	{ "DNSRCH",	RES_DNSRCH },
	{ "INSECURE1",	RES_INSECURE1 },
	{ "INSECURE2",	RES_INSECURE2 },
	{ "NOALIASES",	RES_NOALIASES },
	{ "USE_INET6",	RES_USE_INET6 },
	{ "USE_EDNS0",	RES_USE_EDNS0 },
	{ "USE_DNSSEC",	RES_USE_DNSSEC },
	{ NULL, 	0 },
};

const char *
rcodetostr(uint16_t v)
{
	static char	buf[16];
	size_t		i;

	for(i = 0; kv_rcode[i].key; i++)
		if (kv_rcode[i].value == v)
			return (kv_rcode[i].key);

	snprintf(buf, sizeof buf, "%"PRIu16"?", v);

	return (buf);
}

const char *
typetostr(uint16_t v)
{
	static char	 buf[16];
	size_t		 i;

	for(i = 0; kv_type[i].key; i++)
		if (kv_type[i].value == v)
			return (kv_type[i].key);

	snprintf(buf, sizeof buf, "%"PRIu16"?", v);

	return (buf);
}

const char *
classtostr(uint16_t v)
{
	static char	 buf[16];
	size_t		 i;

	for(i = 0; kv_class[i].key; i++)
		if (kv_class[i].value == v)
			return (kv_class[i].key);

	snprintf(buf, sizeof buf, "%"PRIu16"?", v);

	return (buf);
}

uint16_t
strtotype(const char *name)
{
	size_t	i;

	for(i = 0; kv_type[i].key; i++)
		if (!strcasecmp(kv_type[i].key, name))
			return (kv_type[i].value);

	return (0);
}

uint16_t
strtoclass(const char *name)
{
	size_t	i;

	for(i = 0; kv_class[i].key; i++)
		if (!strcasecmp(kv_class[i].key, name))
			return (kv_class[i].value);

	return (0);
}

int
strtoresopt(const char *name)
{
	size_t	i;

	for(i = 0; kv_resopt[i].key; i++)
		if (!strcasecmp(kv_resopt[i].key, name))
			return (kv_resopt[i].value);

	return (0);
}

void
parseresopt(const char *name)
{
	static int init = 0;
	int flag, neg = 0;

	if (init == 0) {
		res_init();
		init = 1;
	}

	if (name[0] == '-') {
		neg = 1;
		name++;
	}
	else if (name[0] == '+')
		name++;

	flag = strtoresopt(name);
	if (flag == 0)
		errx(1, "unknown reslover option %s", name);
	
	if (neg)
		_res.options &= ~flag;
	else
		_res.options |= flag;
}

void
print_hostent(struct hostent *e)
{
	char	buf[256], **c;

	printf("name = \"%s\"\n", e->h_name);
	printf("aliases =");
	for(c = e->h_aliases; *c; c++)
		printf(" \"%s\"", *c);
	printf("\n");
	printf("addrtype = %i\n", e->h_addrtype);
	printf("addrlength = %i\n", e->h_length);
	printf("addr_list =");
	for(c = e->h_addr_list; *c; c++) {
		printf(" %s", inet_ntop(e->h_addrtype, *c, buf, sizeof buf));
	}
	printf("\n");
}

void
print_netent(struct netent *e)
{
	char	buf[256], **c;
	uint32_t addr;

	/* network number are given in host order */
	addr = htonl(e->n_net);

	printf("name = \"%s\"\n", e->n_name);
	printf("aliases =");
	for (c = e->n_aliases; *c; c++)
		printf(" \"%s\"", *c);
	printf("\n");
	printf("addrtype = %i\n", e->n_addrtype);
	printf("net = %s\n", inet_ntop(e->n_addrtype, &addr, buf, sizeof buf));
}

void
print_addrinfo(struct addrinfo *ai)
{
	char	buf[256], bf[64], bt[64], bp[64];

	printf("family=%s socktype=%s protocol=%s addr=%s canonname=%s\n",
		kv_lookup_name(kv_family, ai->ai_family, bf, sizeof bf),
		kv_lookup_name(kv_socktype, ai->ai_socktype, bt, sizeof bt),
		kv_lookup_name(kv_protocol, ai->ai_protocol, bp, sizeof bp),
		print_addr(ai->ai_addr, buf, sizeof buf),
		ai->ai_canonname);
}

const char *
rrsetstrerror(int e)
{
	switch (e) {
	case 0:
		return "OK";
	case ERRSET_NONAME:
		return "ERRSET_NONAME";
	case ERRSET_NODATA:
		return "ERRSET_NODATA";
	case ERRSET_NOMEMORY:
		return "ERRSET_NOMEMORY";
	case ERRSET_INVAL:
		return "ERRSET_INVAL";
	case ERRSET_FAIL:
		return "ERRSET_FAIL";
	default:
		return "???";
	}
}

void
print_rrsetinfo(struct rrsetinfo * rrset)
{
	printf("rri_flags=%u\n", rrset->rri_flags);
	printf("rri_rdclass=%u\n", rrset->rri_rdclass);
	printf("rri_rdtype=%u\n", rrset->rri_rdtype);
	printf("rri_ttl=%u\n", rrset->rri_ttl);
	printf("rri_nrdatas=%u\n", rrset->rri_nrdatas);
	printf("rri_nsigs=%u\n", rrset->rri_nsigs);
	printf("rri_name=\"%s\"\n", rrset->rri_name);
}

void
print_errors(void)
{
	switch (long_err) {
	case 0:
		return;
	case 1:
		printf("  => errno %i, h_errno %i", errno, h_errno);
		printf(", rrset_errno %i", rrset_errno);
		printf(", gai_errno %i", gai_errno);
		printf ("\n");
		return;
	default:
		printf("  => errno %i: %s\n  => h_errno %i: %s\n  => rrset_errno %i: %s\n",
		    errno, errno ? strerror(errno) : "ok",
		    h_errno, h_errno ? hstrerror(h_errno) : "ok",
		    rrset_errno, rrset_errno ? rrsetstrerror(rrset_errno) : "ok");
		printf("  => gai_errno %i: %s\n",
		    gai_errno, gai_errno ? gai_strerror(gai_errno) : "ok");
	}
}


static char *
print_host(const struct sockaddr *sa, char *buf, size_t len)
{
	switch (sa->sa_family) {
	case AF_INET:
		inet_ntop(AF_INET, &((struct sockaddr_in*)sa)->sin_addr,
			  buf, len);
		break;
	case AF_INET6:
		inet_ntop(AF_INET6, &((struct sockaddr_in6*)sa)->sin6_addr,
			  buf, len);
		break;
	default:
		buf[0] = '\0';
	}
	return (buf);
}


char *
print_addr(const struct sockaddr *sa, char *buf, size_t len)
{
	char	h[256];

	print_host(sa, h, sizeof h);

	switch (sa->sa_family) {
	case AF_INET:
		snprintf(buf, len, "%s:%i", h,
		    ntohs(((struct sockaddr_in*)(sa))->sin_port));
		break;
	case AF_INET6:
		snprintf(buf, len, "[%s]:%i", h,
		    ntohs(((struct sockaddr_in6*)(sa))->sin6_port));
		break;
	default:
		snprintf(buf, len, "?");
		break;
	}

	return (buf);
}

void
packed_init(struct packed *pack, char *data, size_t len)
{
	pack->data = data;
	pack->len = len;
	pack->offset = 0;
	pack->err = NULL;
}


static ssize_t
dname_expand(const unsigned char *data, size_t len, size_t offset,
    size_t *newoffset, char *dst, size_t max)
{
	size_t		 n, count, end, ptr, start;
	ssize_t		 res;

	if (offset >= len)
		return (-1);

	res = 0;
	end = start = offset;

	for(; (n = data[offset]); ) {
		if ((n & 0xc0) == 0xc0) {
			if (offset + 2 > len)
				return (-1);
			ptr = 256 * (n & ~0xc0) + data[offset + 1];
			if (ptr >= start)
				return (-1);
			if (end < offset + 2)
				end = offset + 2;
			offset = ptr;
			continue;
		}
		if (offset + n + 1 > len)
			return (-1);


		/* copy n + at offset+1 */
		if (dst != NULL && max != 0) {
			count = (max < n + 1) ? (max) : (n + 1);
			memmove(dst, data + offset, count);
			dst += count;
			max -= count;
		}
		res += n + 1;
		offset += n + 1;
		if (end < offset)
			end = offset;
	}
	if (end < offset + 1)
		end = offset + 1;

	if (dst != NULL && max != 0)
		dst[0] = 0;
	if (newoffset)
		*newoffset = end;
	return (res + 1);
}

static int
unpack_data(struct packed *p, void *data, size_t len)
{
	if (p->err)
		return (-1);

	if (p->len - p->offset < len) {
		p->err = "too short";
		return (-1);
	}

	memmove(data, p->data + p->offset, len);
	p->offset += len;

	return (0);
}

static int
unpack_u16(struct packed *p, uint16_t *u16)
{
	if (unpack_data(p, u16, 2) == -1)
		return (-1);

	*u16 = ntohs(*u16);

	return (0);
}

static int
unpack_u32(struct packed *p, uint32_t *u32)
{
	if (unpack_data(p, u32, 4) == -1)
		return (-1);

	*u32 = ntohl(*u32);

	return (0);
}

static int
unpack_inaddr(struct packed *p, struct in_addr *a)
{
	return (unpack_data(p, a, 4));
}

static int
unpack_in6addr(struct packed *p, struct in6_addr *a6)
{
	return (unpack_data(p, a6, 16));
}

static int
unpack_dname(struct packed *p, char *dst, size_t max)
{
	ssize_t e;

	if (p->err)
		return (-1);

	e = dname_expand(p->data, p->len, p->offset, &p->offset, dst, max);
	if (e == -1) {
		p->err = "bad domain name";
		return (-1);
	}
	if (e < 0 || e > MAXDNAME) {
		p->err = "domain name too long";
		return (-1);
	}

	return (0);
}

int
unpack_header(struct packed *p, struct header *h)
{
	if (unpack_data(p, h, HFIXEDSZ) == -1)
		return (-1);

	h->flags = ntohs(h->flags);
	h->qdcount = ntohs(h->qdcount);
	h->ancount = ntohs(h->ancount);
	h->nscount = ntohs(h->nscount);
	h->arcount = ntohs(h->arcount);

	return (0);
}

int
unpack_query(struct packed *p, struct query *q)
{
	unpack_dname(p, q->q_dname, sizeof(q->q_dname));
	unpack_u16(p, &q->q_type);
	unpack_u16(p, &q->q_class);

	return (p->err) ? (-1) : (0);
}

int
unpack_rr(struct packed *p, struct rr *rr)
{
	uint16_t	rdlen;
	size_t		save_offset;

	unpack_dname(p, rr->rr_dname, sizeof(rr->rr_dname));
	unpack_u16(p, &rr->rr_type);
	unpack_u16(p, &rr->rr_class);
	unpack_u32(p, &rr->rr_ttl);
	unpack_u16(p, &rdlen);

	if (p->err)
		return (-1);

	if (p->len - p->offset < rdlen) {
		p->err = "too short";
		return (-1);
	}

	save_offset = p->offset;

	switch(rr->rr_type) {

	case T_CNAME:
		unpack_dname(p, rr->rr.cname.cname, sizeof(rr->rr.cname.cname));
		break;

	case T_MX:
		unpack_u16(p, &rr->rr.mx.preference);
		unpack_dname(p, rr->rr.mx.exchange, sizeof(rr->rr.mx.exchange));
		break;

	case T_NS:
		unpack_dname(p, rr->rr.ns.nsname, sizeof(rr->rr.ns.nsname));
		break;

	case T_PTR:
		unpack_dname(p, rr->rr.ptr.ptrname, sizeof(rr->rr.ptr.ptrname));
		break;

	case T_SOA:
		unpack_dname(p, rr->rr.soa.mname, sizeof(rr->rr.soa.mname));
		unpack_dname(p, rr->rr.soa.rname, sizeof(rr->rr.soa.rname));
		unpack_u32(p, &rr->rr.soa.serial);
		unpack_u32(p, &rr->rr.soa.refresh);
		unpack_u32(p, &rr->rr.soa.retry);
		unpack_u32(p, &rr->rr.soa.expire);
		unpack_u32(p, &rr->rr.soa.minimum);
		break;

	case T_A:
		if (rr->rr_class != C_IN)
			goto other;
		unpack_inaddr(p, &rr->rr.in_a.addr);
		break;

	case T_AAAA:
		if (rr->rr_class != C_IN)
			goto other;
		unpack_in6addr(p, &rr->rr.in_aaaa.addr6);
		break;
	default:
	other:
		rr->rr.other.rdata = p->data + p->offset;
		rr->rr.other.rdlen = rdlen;
		p->offset += rdlen;
	}

	if (p->err)
		return (-1);

	/* make sure that the advertised rdlen is really ok */
	if (p->offset - save_offset != rdlen)
		p->err = "bad dlen";

	return (p->err) ? (-1) : (0);
}

int
sockaddr_from_str(struct sockaddr *sa, int family, const char *str)
{
	struct in_addr		 ina;
	struct in6_addr		 in6a;
	struct sockaddr_in	*sin;
	struct sockaddr_in6	*sin6;

	switch (family) {
	case PF_UNSPEC:
		if (sockaddr_from_str(sa, PF_INET, str) == 0)
			return (0);
		return sockaddr_from_str(sa, PF_INET6, str);

	case PF_INET:
		if (inet_pton(PF_INET, str, &ina) != 1)
			return (-1);

		sin = (struct sockaddr_in *)sa;
		memset(sin, 0, sizeof *sin);
		sin->sin_len = sizeof(struct sockaddr_in);
		sin->sin_family = PF_INET;
		sin->sin_addr.s_addr = ina.s_addr;
		return (0);

	case PF_INET6:
		if (inet_pton(PF_INET6, str, &in6a) != 1)
			return (-1);

		sin6 = (struct sockaddr_in6 *)sa;
		memset(sin6, 0, sizeof *sin6);
		sin6->sin6_len = sizeof(struct sockaddr_in6);
		sin6->sin6_family = PF_INET6;
		sin6->sin6_addr = in6a;
		return (0);

	default:
		break;
	}

	return (-1);
}
