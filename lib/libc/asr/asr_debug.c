/*	$OpenBSD: asr_debug.c,v 1.28 2021/11/22 20:18:27 jca Exp $	*/
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
#include <arpa/nameser.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <asr.h>
#include <resolv.h>
#include <string.h>

#include "asr_private.h"

static const char *rcodetostr(uint16_t);
static const char *print_dname(const char *, char *, size_t);
static const char *print_header(const struct asr_dns_header *, char *, size_t);
static const char *print_query(const struct asr_dns_query *, char *, size_t);
static const char *print_rr(const struct asr_dns_rr *, char *, size_t);

FILE *_asr_debug = NULL;

#define OPCODE_SHIFT	11

static const char *
rcodetostr(uint16_t v)
{
	switch (v) {
	case NOERROR:	return "NOERROR";
	case FORMERR:	return "FORMERR";
	case SERVFAIL:	return "SERVFAIL";
	case NXDOMAIN:	return "NXDOMAIN";
	case NOTIMP:	return "NOTIMP";
	case REFUSED:	return "REFUSED";
	default:	return "?";
	}
}

static const char *
print_dname(const char *_dname, char *buf, size_t max)
{
	return (_asr_strdname(_dname, buf, max));
}

static const char *
print_rr(const struct asr_dns_rr *rr, char *buf, size_t max)
{
	char	*res;
	char	 tmp[256];
	char	 tmp2[256];
	int	 r;

	res = buf;

	r = snprintf(buf, max, "%s %u %s %s ",
	    print_dname(rr->rr_dname, tmp, sizeof tmp),
	    rr->rr_ttl,
	    __p_class(rr->rr_class),
	    __p_type(rr->rr_type));
	if (r < 0 || r >= max) {
		buf[0] = '\0';
		return (buf);
	}

	if ((size_t)r >= max)
		return (buf);

	max -= r;
	buf += r;

	switch (rr->rr_type) {
	case T_CNAME:
		print_dname(rr->rr.cname.cname, buf, max);
		break;
	case T_MX:
		snprintf(buf, max, "%lu %s",
		    (unsigned long)rr->rr.mx.preference,
		    print_dname(rr->rr.mx.exchange, tmp, sizeof tmp));
		break;
	case T_NS:
		print_dname(rr->rr.ns.nsname, buf, max);
		break;
	case T_PTR:
		print_dname(rr->rr.ptr.ptrname, buf, max);
		break;
	case T_SOA:
		snprintf(buf, max, "%s %s %lu %lu %lu %lu %lu",
		    print_dname(rr->rr.soa.mname, tmp, sizeof tmp),
		    print_dname(rr->rr.soa.rname, tmp2, sizeof tmp2),
		    (unsigned long)rr->rr.soa.serial,
		    (unsigned long)rr->rr.soa.refresh,
		    (unsigned long)rr->rr.soa.retry,
		    (unsigned long)rr->rr.soa.expire,
		    (unsigned long)rr->rr.soa.minimum);
		break;
	case T_A:
		if (rr->rr_class != C_IN)
			goto other;
		snprintf(buf, max, "%s", inet_ntop(AF_INET,
		    &rr->rr.in_a.addr, tmp, sizeof tmp));
		break;
	case T_AAAA:
		if (rr->rr_class != C_IN)
			goto other;
		snprintf(buf, max, "%s", inet_ntop(AF_INET6,
		    &rr->rr.in_aaaa.addr6, tmp, sizeof tmp));
		break;
	default:
	other:
		snprintf(buf, max, "(rdlen=%i)", (int)rr->rr.other.rdlen);
		break;
	}

	return (res);
}

static const char *
print_query(const struct asr_dns_query *q, char *buf, size_t max)
{
	char b[256];

	snprintf(buf, max, "%s	%s %s",
	    print_dname(q->q_dname, b, sizeof b),
	    __p_class(q->q_class), __p_type(q->q_type));

	return (buf);
}

static const char *
print_header(const struct asr_dns_header *h, char *buf, size_t max)
{
	snprintf(buf, max,
	"id:0x%04x %s op:%i %s %s %s %s z:%i %s %s r:%s qd:%i an:%i ns:%i ar:%i",
	    ((int)h->id),
	    (h->flags & QR_MASK) ? "QR":"  ",
	    (int)(OPCODE(h->flags) >> OPCODE_SHIFT),
	    (h->flags & AA_MASK) ? "AA":"  ",
	    (h->flags & TC_MASK) ? "TC":"  ",
	    (h->flags & RD_MASK) ? "RD":"  ",
	    (h->flags & RA_MASK) ? "RA":"  ",
	    (h->flags & Z_MASK),
	    (h->flags & AD_MASK) ? "AD":"  ",
	    (h->flags & CD_MASK) ? "CD":"  ",
	    rcodetostr(RCODE(h->flags)),
	    h->qdcount, h->ancount, h->nscount, h->arcount);

	return (buf);
}

void
_asr_dump_packet(FILE *f, const void *data, size_t len)
{
	char			buf[1024];
	struct asr_unpack	p;
	struct asr_dns_header	h;
	struct asr_dns_query	q;
	struct asr_dns_rr	rr;
	int			i, an, ns, ar, n;

	if (f == NULL)
		return;

	_asr_unpack_init(&p, data, len);

	if (_asr_unpack_header(&p, &h) == -1) {
		fprintf(f, ";; BAD PACKET: %s\n", strerror(p.err));
		return;
	}

	fprintf(f, ";; HEADER %s\n", print_header(&h, buf, sizeof buf));

	if (h.qdcount)
		fprintf(f, ";; QUERY SECTION:\n");
	for (i = 0; i < h.qdcount; i++) {
		if (_asr_unpack_query(&p, &q) == -1)
			goto error;
		fprintf(f, "%s\n", print_query(&q, buf, sizeof buf));
	}

	an = 0;
	ns = an + h.ancount;
	ar = ns + h.nscount;
	n = ar + h.arcount;

	for (i = 0; i < n; i++) {
		if (i == an)
			fprintf(f, "\n;; ANSWER SECTION:\n");
		if (i == ns)
			fprintf(f, "\n;; AUTHORITY SECTION:\n");
		if (i == ar)
			fprintf(f, "\n;; ADDITIONAL SECTION:\n");

		if (_asr_unpack_rr(&p, &rr) == -1)
			goto error;
		fprintf(f, "%s\n", print_rr(&rr, buf, sizeof buf));
	}

	if (p.offset != len)
		fprintf(f, ";; REMAINING GARBAGE %zu\n", len - p.offset);

    error:
	if (p.err)
		fprintf(f, ";; ERROR AT OFFSET %zu/%zu: %s\n", p.offset, p.len,
		    strerror(p.err));
}

const char *
_asr_print_sockaddr(const struct sockaddr *sa, char *buf, size_t len)
{
	char	h[256];
	int	portno;
	union {
		const struct sockaddr		*sa;
		const struct sockaddr_in	*sin;
		const struct sockaddr_in6	*sin6;
	}	s;

	s.sa = sa;

	switch (sa->sa_family) {
	case AF_INET:
		inet_ntop(AF_INET, &s.sin->sin_addr, h, sizeof h);
		portno = ntohs(s.sin->sin_port);
		break;
	case AF_INET6:
		inet_ntop(AF_INET6, &s.sin6->sin6_addr, h, sizeof h);
		portno = ntohs(s.sin6->sin6_port);
		break;
	default:
		snprintf(buf, len, "?");
		return (buf);
	}

	snprintf(buf, len, "%s:%i", h, portno);
	return (buf);
}

void
_asr_dump_config(FILE *f, struct asr *a)
{
	char		 buf[256];
	int		 i;
	struct asr_ctx	*ac;
	unsigned int	 o;

	if (f == NULL)
		return;

	ac = a->a_ctx;

	fprintf(f, "--------- ASR CONFIG ---------------\n");
	fprintf(f, "DOMAIN \"%s\"\n", ac->ac_domain);
	fprintf(f, "SEARCH\n");
	for (i = 0; i < ac->ac_domcount; i++)
		fprintf(f, "   \"%s\"\n", ac->ac_dom[i]);
	fprintf(f, "OPTIONS\n");
	fprintf(f, " options:");
	o = ac->ac_options;

#define PRINTOPT(flag, n) if (o & (flag)) { fprintf(f, " " n); o &= ~(flag); }
	PRINTOPT(RES_INIT, "INIT");
	PRINTOPT(RES_DEBUG, "DEBUG");
	PRINTOPT(RES_USEVC, "USEVC");
	PRINTOPT(RES_IGNTC, "IGNTC");
	PRINTOPT(RES_RECURSE, "RECURSE");
	PRINTOPT(RES_DEFNAMES, "DEFNAMES");
	PRINTOPT(RES_STAYOPEN, "STAYOPEN");
	PRINTOPT(RES_DNSRCH, "DNSRCH");
	PRINTOPT(RES_NOALIASES, "NOALIASES");
	PRINTOPT(RES_USE_EDNS0, "USE_EDNS0");
	PRINTOPT(RES_USE_DNSSEC, "USE_DNSSEC");
	PRINTOPT(RES_USE_CD, "USE_CD");
	PRINTOPT(RES_TRUSTAD, "TRUSTAD");
	if (o)
		fprintf(f, " 0x%08x", o);
	fprintf(f, "\n");

	fprintf(f, " ndots: %i\n", ac->ac_ndots);
	fprintf(f, " family:");
	for (i = 0; ac->ac_family[i] != -1; i++)
		fprintf(f, " %s", (ac->ac_family[i] == AF_INET)?"inet4":"inet6");
	fprintf(f, "\n");
	fprintf(f, "NAMESERVERS timeout=%i retry=%i\n",
		    ac->ac_nstimeout,
		    ac->ac_nsretries);
	for (i = 0; i < ac->ac_nscount; i++)
		fprintf(f, "	%s\n", _asr_print_sockaddr(ac->ac_ns[i], buf,
		    sizeof buf));
	fprintf(f, "LOOKUP %s", ac->ac_db);
	fprintf(f, "\n------------------------------------\n");
}

#define CASE(n) case n: return #n

const char *
_asr_statestr(int state)
{
	switch (state) {
	CASE(ASR_STATE_INIT);
	CASE(ASR_STATE_NEXT_DOMAIN);
	CASE(ASR_STATE_NEXT_DB);
	CASE(ASR_STATE_SAME_DB);
	CASE(ASR_STATE_NEXT_FAMILY);
	CASE(ASR_STATE_NEXT_NS);
	CASE(ASR_STATE_UDP_SEND);
	CASE(ASR_STATE_UDP_RECV);
	CASE(ASR_STATE_TCP_WRITE);
	CASE(ASR_STATE_TCP_READ);
	CASE(ASR_STATE_PACKET);
	CASE(ASR_STATE_SUBQUERY);
	CASE(ASR_STATE_NOT_FOUND);
	CASE(ASR_STATE_HALT);
	default:
		return "?";
	}
};

const char *
_asr_querystr(int type)
{
	switch (type) {
	CASE(ASR_SEND);
	CASE(ASR_SEARCH);
	CASE(ASR_GETRRSETBYNAME);
	CASE(ASR_GETHOSTBYNAME);
	CASE(ASR_GETHOSTBYADDR);
	CASE(ASR_GETADDRINFO);
	CASE(ASR_GETNAMEINFO);
	default:
		return "?";
	}
}

const char *
_asr_transitionstr(int type)
{
	switch (type) {
	CASE(ASYNC_COND);
	CASE(ASYNC_DONE);
	default:
		return "?";
	}
}
