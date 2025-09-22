/*	$OpenBSD: res_query.c,v 1.4 2022/01/20 14:18:10 naddy Exp $	*/
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
#include <sys/time.h>

#include <netinet/in.h>
#include <arpa/nameser.h>
#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <resolv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

/* in asr.c but we don't want them exposed right now */
static void dump_packet(const void *, size_t);

static char *print_query(struct query *, char *, size_t);
static char *print_rr(struct rr *, char *, size_t);
static char *print_host(const struct sockaddr *, char *, size_t);
static char* print_dname(const char *, char *, size_t);


static int
msec(struct timeval start, struct timeval end)
{
	return (int)((end.tv_sec - start.tv_sec) * 1000
	    + (end.tv_usec - start.tv_usec) / 1000);
}

static void
usage(void)
{
	extern const char * __progname;

	fprintf(stderr, "usage: %s [-deq] [-t type] [host...]\n",
	    __progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct timeval		 start, end;
	time_t			 when;
	int			 ch, i, qflag, dflag, r;
	uint16_t		 type = T_A;
	char			 buf[1024], *host;

	dflag = 0;
	qflag = 0;

	while((ch = getopt(argc, argv, "R:deqt:")) !=  -1) {
		switch(ch) {
		case 'R':
			parseresopt(optarg);
			break;
		case 'd':
			dflag = 1;
			break;
		case 'e':
			long_err += 1;
			break;
		case 'q':
			qflag = 1;
			break;
		case 't':
			if ((type = strtotype(optarg)) == 0)
				usage();
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	for (i = 0; i < argc; i++) {

		if (i)
			printf("\n");

		printf("===> \"%s\"\n", argv[i]);
		host = gethostarg(argv[i]);

		errno = 0;
		h_errno = 0;
		gai_errno = 0;
		rrset_errno = 0;

		if (gettimeofday(&start, NULL) != 0)
			err(1, "gettimeofday");

		if (qflag)
			r = res_query(host, C_IN, type, buf, sizeof(buf));
		else
			r = res_search(host, C_IN, type, buf, sizeof(buf));

		if (gettimeofday(&end, NULL) != 0)
			err(1, "gettimeofday");

		if (r != -1) {
			dump_packet(buf, r);
			printf("\n");
			if (dflag) {
				printf(";; Query time: %d msec\n",
				    msec(start, end));
				when = time(NULL);
				printf(";; WHEN: %s", ctime(&when));
			}
			printf(";; MSG SIZE  rcvd: %i\n", r);
		}
		print_errors();
	}

	return (0);
}

#define OPCODE_SHIFT	11
#define Z_SHIFT		4

static char*
print_header(struct header *h, char *buf, size_t max)
{
	snprintf(buf, max,
	"id:0x.... %s op:%i %s %s %s %s z:%i r:%s qd:%i an:%i ns:%i ar:%i",
	    (h->flags & QR_MASK) ? "QR":"  ",
	    (int)(OPCODE(h->flags) >> OPCODE_SHIFT),
	    (h->flags & AA_MASK) ? "AA":"  ",
	    (h->flags & TC_MASK) ? "TC":"  ",
	    (h->flags & RD_MASK) ? "RD":"  ",
	    (h->flags & RA_MASK) ? "RA":"  ",  
	    ((h->flags & Z_MASK) >> Z_SHIFT),
	    rcodetostr(RCODE(h->flags)),
	    h->qdcount, h->ancount, h->nscount, h->arcount);

	return buf;
}

static void
dump_packet(const void *data, size_t len)
{
	char		buf[1024];
	struct packed	p;
	struct header	h;
	struct query	q;
	struct rr	rr;
	int		i, an, ns, ar, n;

	packed_init(&p, (char *)data, len);

	if (unpack_header(&p, &h) == -1) {
		printf(";; BAD PACKET: %s\n", p.err);
		return;
	}

	printf(";; HEADER %s\n", print_header(&h, buf, sizeof buf));

	if (h.qdcount)
		printf(";; QUERY SECTION:\n");
	for (i = 0; i < h.qdcount; i++) {
		if (unpack_query(&p, &q) == -1)
			goto error;
		printf("%s\n", print_query(&q, buf, sizeof buf));
	}

	an = 0;
	ns = an + h.ancount;
	ar = ns + h.nscount;
	n = ar + h.arcount;

	for (i = 0; i < n; i++) {
		if (i == an)
			printf("\n;; ANSWER SECTION:\n");
		if (i == ns)
			printf("\n;; AUTHORITY SECTION:\n");
		if (i == ar)
			printf("\n;; ADDITIONAL SECTION:\n");

		if (unpack_rr(&p, &rr) == -1)
			goto error;
		printf("%s\n", print_rr(&rr, buf, sizeof buf));
	}

	if (p.offset != len)
		printf(";; REMAINING GARBAGE %zu\n", len - p.offset);

    error:
	if (p.err)
		printf(";; ERROR AT OFFSET %zu/%zu: %s\n", p.offset, p.len,
		    p.err);
}

static const char *
inet6_ntoa(struct in6_addr a)
{
	static char buf[256];
	struct sockaddr_in6	si;

	si.sin6_len = sizeof(si);
	si.sin6_family = PF_INET6;
	si.sin6_addr = a;

	return print_host((struct sockaddr*)&si, buf, sizeof buf);
}

static char*
print_rr(struct rr *rr, char *buf, size_t max)
{
	char	*res;
	char	 tmp[256];
	char	 tmp2[256];
	int	 r;

	res = buf;

	r = snprintf(buf, max, "%s %u %s %s ",
	    print_dname(rr->rr_dname, tmp, sizeof tmp),
	    rr->rr_ttl,
	    classtostr(rr->rr_class),
	    typetostr(rr->rr_type));
	if (r == -1) {
		buf[0] = '\0';
		return buf;
	}

	if ((size_t)r >= max)
		return buf;

	max -= r;
	buf += r;

	switch(rr->rr_type) {
	case T_CNAME:
		print_dname(rr->rr.cname.cname, buf, max);
		break;
	case T_MX:
		snprintf(buf, max, "%"PRIu32" %s",
		    rr->rr.mx.preference,
		    print_dname(rr->rr.mx.exchange, tmp, sizeof tmp));
		break;
	case T_NS:
		print_dname(rr->rr.ns.nsname, buf, max);
		break;
	case T_PTR:
		print_dname(rr->rr.ptr.ptrname, buf, max);
		break;
	case T_SOA:
		snprintf(buf, max,
		    "%s %s %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32,
		    print_dname(rr->rr.soa.rname, tmp, sizeof tmp),
		    print_dname(rr->rr.soa.mname, tmp2, sizeof tmp2),
		    rr->rr.soa.serial,
		    rr->rr.soa.refresh,
		    rr->rr.soa.retry,
		    rr->rr.soa.expire,
		    rr->rr.soa.minimum);
		break;
	case T_A:
		if (rr->rr_class != C_IN)
			goto other;
		snprintf(buf, max, "%s", inet_ntoa(rr->rr.in_a.addr));
		break;
	case T_AAAA:
		if (rr->rr_class != C_IN)
			goto other;
		snprintf(buf, max, "%s", inet6_ntoa(rr->rr.in_aaaa.addr6));
		break;
	default:
	other:
		snprintf(buf, max, "(rdlen=%"PRIu16 ")", rr->rr.other.rdlen);
		break;
	}

	return (res);
}

static char*
print_query(struct query *q, char *buf, size_t max)
{
	char b[256];

	snprintf(buf, max, "%s	%s %s",
	    print_dname(q->q_dname, b, sizeof b),
	    classtostr(q->q_class), typetostr(q->q_type));

	return (buf);
}


static char *
print_host(const struct sockaddr *sa, char *buf, size_t len)
{
	switch (sa->sa_family) {
	case AF_INET:
		inet_ntop(AF_INET, &((struct sockaddr_in*)sa)->sin_addr, buf, len);
		break;
	case AF_INET6:
		inet_ntop(AF_INET6, &((struct sockaddr_in6*)sa)->sin6_addr, buf, len);
		break;
	default:
		buf[0] = '\0';
	}
	return (buf);
}

static char*
print_dname(const char *_dname, char *buf, size_t max)
{
	const unsigned char *dname = _dname;
	char	*res;
	size_t	 left, count;

	if (_dname[0] == 0) {
		strlcpy(buf, ".", max);
		return buf;
	}

	res = buf;
	left = max - 1;
	while (dname[0] && left) {
		count = (dname[0] < (left - 1)) ? dname[0] : (left - 1);
		memmove(buf, dname + 1, count);
		dname += dname[0] + 1;
		left -= count;
		buf += count;
		if (left) {
			left -= 1;
			*buf++ = '.';
		}
	}
	buf[0] = 0;

	return (res);
}
