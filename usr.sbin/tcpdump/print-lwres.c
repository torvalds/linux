/*
 * Copyright (C) 2001 WIDE Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/time.h>

#include <netinet/in.h>

#ifdef NOERROR
#undef NOERROR					/* Solaris sucks */
#endif
#ifdef NOERROR
#undef T_UNSPEC					/* SINIX does too */
#endif
#include "nameser.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <resolv.h>			/* for b64_ntop() proto */

#include "interface.h"
#include "addrtoname.h"
#include "extract.h"                    /* must come after interface.h */

/* BIND9 lib/lwres/include/lwres */
typedef u_int32_t lwres_uint32_t;
typedef u_int16_t lwres_uint16_t;
typedef u_int8_t lwres_uint8_t;

struct lwres_lwpacket {
	lwres_uint32_t		length;
	lwres_uint16_t		version;
	lwres_uint16_t		pktflags;
	lwres_uint32_t		serial;
	lwres_uint32_t		opcode;
	lwres_uint32_t		result;
	lwres_uint32_t		recvlength;
	lwres_uint16_t		authtype;
	lwres_uint16_t		authlength;
};

#define LWRES_LWPACKETFLAG_RESPONSE	0x0001U	/* if set, pkt is a response */

#define LWRES_LWPACKETVERSION_0		0

#define LWRES_FLAG_TRUSTNOTREQUIRED	0x00000001U
#define LWRES_FLAG_SECUREDATA		0x00000002U

/*
 * no-op
 */
#define LWRES_OPCODE_NOOP		0x00000000U

typedef struct {
	/* public */
	lwres_uint16_t			datalength;
	/* data follows */
} lwres_nooprequest_t;

typedef struct {
	/* public */
	lwres_uint16_t			datalength;
	/* data follows */
} lwres_noopresponse_t;

/*
 * get addresses by name
 */
#define LWRES_OPCODE_GETADDRSBYNAME	0x00010001U

typedef struct lwres_addr lwres_addr_t;

struct lwres_addr {
	lwres_uint32_t			family;
	lwres_uint16_t			length;
	/* address follows */
};

typedef struct {
	/* public */
	lwres_uint32_t			flags;
	lwres_uint32_t			addrtypes;
	lwres_uint16_t			namelen;
	/* name follows */
} lwres_gabnrequest_t;

typedef struct {
	/* public */
	lwres_uint32_t			flags;
	lwres_uint16_t			naliases;
	lwres_uint16_t			naddrs;
	lwres_uint16_t			realnamelen;
	/* aliases follows */
	/* addrs follows */
	/* realname follows */
} lwres_gabnresponse_t;

/*
 * get name by address
 */
#define LWRES_OPCODE_GETNAMEBYADDR	0x00010002U
typedef struct {
	/* public */
	lwres_uint32_t			flags;
	lwres_addr_t			addr;
	/* addr body follows */
} lwres_gnbarequest_t;

typedef struct {
	/* public */
	lwres_uint32_t			flags;
	lwres_uint16_t			naliases;
	lwres_uint16_t			realnamelen;
	/* aliases follows */
	/* realname follows */
} lwres_gnbaresponse_t;

/*
 * get rdata by name
 */
#define LWRES_OPCODE_GETRDATABYNAME	0x00010003U

typedef struct {
	/* public */
	lwres_uint32_t			flags;
	lwres_uint16_t			rdclass;
	lwres_uint16_t			rdtype;
	lwres_uint16_t			namelen;
	/* name follows */
} lwres_grbnrequest_t;

typedef struct {
	/* public */
	lwres_uint32_t			flags;
	lwres_uint16_t			rdclass;
	lwres_uint16_t			rdtype;
	lwres_uint32_t			ttl;
	lwres_uint16_t			nrdatas;
	lwres_uint16_t			nsigs;
	/* realname here (len + name) */
	/* rdata here (len + name) */
	/* signatures here (len + name) */
} lwres_grbnresponse_t;

#define LWRDATA_VALIDATED	0x00000001

#define LWRES_ADDRTYPE_V4		0x00000001U	/* ipv4 */
#define LWRES_ADDRTYPE_V6		0x00000002U	/* ipv6 */

#define LWRES_MAX_ALIASES		16		/* max # of aliases */
#define LWRES_MAX_ADDRS			64		/* max # of addrs */

struct tok opcode[] = {
	{ LWRES_OPCODE_NOOP,		"noop", },
	{ LWRES_OPCODE_GETADDRSBYNAME,	"getaddrsbyname", },
	{ LWRES_OPCODE_GETNAMEBYADDR,	"getnamebyaddr", },
	{ LWRES_OPCODE_GETRDATABYNAME,	"getrdatabyname", },
	{ 0, 				NULL, },
};

/* print-domain.c */
extern struct tok ns_type2str[];
extern struct tok ns_class2str[];

static int lwres_printname(size_t, const char *);
static int lwres_printnamelen(const char *);
/* static int lwres_printbinlen(const char *); */
static int lwres_printb64len(const char *);
static int lwres_printaddr(lwres_addr_t *);

static int
lwres_printname(size_t l, const char *p0)
{
	const char *p;
	int i;

	p = p0;
	/* + 1 for terminating \0 */
	if (p + l + 1 > (const char *)snapend)
		goto trunc;

	printf(" ");
	for (i = 0; i < l; i++)
		safeputchar(*p++);
	p++;	/* skip terminating \0 */

	return p - p0;

  trunc:
	return -1;
}

static int
lwres_printnamelen(const char *p)
{
	u_int16_t l;
	int advance;

	if (p + 2 > (const char *)snapend)
		goto trunc;
	l = EXTRACT_16BITS(p);
	advance = lwres_printname(l, p + 2);
	if (advance < 0)
		goto trunc;
	return 2 + advance;

  trunc:
	return -1;
}

#if 0
static int
lwres_printbinlen(const char *p0)
{
	u_int8_t *p;
	u_int16_t l;
	int i;

	p = (u_int8_t *)p0;
	if (p + 2 > (u_int8_t *)snapend)
		goto trunc;
	l = EXTRACT_16BITS(p);
	if (p + 2 + l > (u_int8_t *)snapend)
		goto trunc;
	p += 2;
	for (i = 0; i < l; i++)
		printf("%02x", *p++);
	return p - (u_int8_t *)p0;

  trunc:
	return -1;
}
#endif

static int
lwres_printb64len(const char *p0)
{
	u_int8_t *p;
	u_int16_t l;
	char *dbuf, *b64buf;
	int i, b64len;

	p = (u_int8_t *)p0;
	if (p + 2 > (u_int8_t *)snapend)
		goto trunc;
	l = EXTRACT_16BITS(p);
	if (p + 2 + l > (u_int8_t *)snapend)
		goto trunc;

	dbuf = malloc(l);
	if (!dbuf)
	  return -1;

	b64len = (l + 2) / 3 * 4 + 1;
	b64buf = malloc(b64len);
	if (!b64buf)
	  {
	    free(dbuf);
	    return -1;
	  }

	memcpy(dbuf, p, l);

	i = b64_ntop (dbuf, l, b64buf, b64len);
	if (i != -1)
		printf ("%s", b64buf);

	free (dbuf);
	free (b64buf);

	if (i == -1)
		return -1;

	return l + 2;

  trunc:
	return -1;
}

static int
lwres_printaddr(lwres_addr_t *ap)
{
	u_int16_t l;
	const char *p;
	int i;

	TCHECK(ap->length);
	l = ntohs(ap->length);
	/* XXX ap points to packed struct */
	p = (const char *)&ap->length + sizeof(ap->length);
	if (p + l > (const char *)snapend)
		goto trunc;

	switch (ntohl(ap->family)) {
	case 1:	/* IPv4 */
		printf(" %s", ipaddr_string(p));
		p += sizeof(struct in_addr);
		break;
	case 2:	/* IPv6 */
		printf(" %s", ip6addr_string(p));
		p += sizeof(struct in6_addr);
		break;
	default:
		printf(" %lu/", (unsigned long)ntohl(ap->family));
		for (i = 0; i < l; i++)
			printf("%02x", *p++);
	}

	return p - (const char *)ap;

  trunc:
	return -1;
}

void
lwres_print(const u_char *bp, u_int length)
{
	const struct lwres_lwpacket *np;
	u_int32_t v;
	const char *s;
	int response;
	int advance;
	int unsupported = 0;

	np = (const struct lwres_lwpacket *)bp;
	TCHECK(np->authlength);

	printf("lwres");
	v = ntohs(np->version);
	if (vflag || v != LWRES_LWPACKETVERSION_0)
		printf(" v%u", v);
	if (v != LWRES_LWPACKETVERSION_0) {
		s = (const char *)np + ntohl(np->length);
		goto tail;
	}

	response = ntohs(np->pktflags) & LWRES_LWPACKETFLAG_RESPONSE;

	/* opcode and pktflags */
	v = (u_int32_t)ntohl(np->opcode);
	s = tok2str(opcode, "#0x%x", v);
	printf(" %s%s", s, response ? "" : "?");

	/* pktflags */
	v = ntohs(np->pktflags);
	if (v & ~LWRES_LWPACKETFLAG_RESPONSE)
		printf("[0x%x]", v);

	if (vflag > 1) {
		printf(" (");	/*)*/
		printf("serial:0x%lx", (unsigned long)ntohl(np->serial));
		printf(" result:0x%lx", (unsigned long)ntohl(np->result));
		printf(" recvlen:%lu", (unsigned long)ntohl(np->recvlength));
		/* BIND910: not used */
		if (vflag > 2) {
			printf(" authtype:0x%x", ntohs(np->authtype));
			printf(" authlen:%u", ntohs(np->authlength));
		}
		/*(*/
		printf(")");
	}

	/* per-opcode content */
	if (!response) {
		/*
		 * queries
		 */
		lwres_gabnrequest_t *gabn;
		lwres_gnbarequest_t *gnba;
		u_int32_t l;

		gabn = NULL;
		gnba = NULL;

		switch (ntohl(np->opcode)) {
		case LWRES_OPCODE_NOOP:
			break;
		case LWRES_OPCODE_GETADDRSBYNAME:
			gabn = (lwres_gabnrequest_t *)(np + 1);
			TCHECK(gabn->namelen);
			/* XXX gabn points to packed struct */
			s = (const char *)&gabn->namelen +
			    sizeof(gabn->namelen);
			l = ntohs(gabn->namelen);

			/* BIND910: not used */
			if (vflag > 2) {
				printf(" flags:0x%lx",
				    (unsigned long)ntohl(gabn->flags));
			}

			v = (u_int32_t)ntohl(gabn->addrtypes);
			switch (v & (LWRES_ADDRTYPE_V4 | LWRES_ADDRTYPE_V6)) {
			case LWRES_ADDRTYPE_V4:
				printf(" IPv4");
				break;
			case LWRES_ADDRTYPE_V6:
				printf(" IPv6");
				break;
			case LWRES_ADDRTYPE_V4 | LWRES_ADDRTYPE_V6:
				printf(" IPv4/6");
				break;
			}
			if (v & ~(LWRES_ADDRTYPE_V4 | LWRES_ADDRTYPE_V6))
				printf("[0x%x]", v);

			advance = lwres_printname(l, s);
			if (advance < 0)
				goto trunc;
			s += advance;
			break;
		case LWRES_OPCODE_GETNAMEBYADDR:
			gnba = (lwres_gnbarequest_t *)(np + 1);
			TCHECK(gnba->addr);

			/* BIND910: not used */
			if (vflag > 2) {
				printf(" flags:0x%lx",
				    (unsigned long)ntohl(gnba->flags));
			}

			s = (const char *)&gnba->addr;

			advance = lwres_printaddr(&gnba->addr);
			if (advance < 0)
				goto trunc;
			s += advance;
			break;
		default:
			unsupported++;
			break;
		}
	} else {
		/*
		 * responses
		 */
		lwres_gabnresponse_t *gabn;
		lwres_gnbaresponse_t *gnba;
		lwres_grbnresponse_t *grbn;
		u_int32_t l, na;
		int i;

		gabn = NULL;
		gnba = NULL;
		grbn = NULL;

		switch (ntohl(np->opcode)) {
		case LWRES_OPCODE_NOOP:
			break;
		case LWRES_OPCODE_GETADDRSBYNAME:
			gabn = (lwres_gabnresponse_t *)(np + 1);
			TCHECK(gabn->realnamelen);
			/* XXX gabn points to packed struct */
			s = (const char *)&gabn->realnamelen +
			    sizeof(gabn->realnamelen);
			l = ntohs(gabn->realnamelen);

			/* BIND910: not used */
			if (vflag > 2) {
				printf(" flags:0x%lx",
				    (unsigned long)ntohl(gabn->flags));
			}

			printf(" %u/%u", ntohs(gabn->naliases),
			    ntohs(gabn->naddrs));

			advance = lwres_printname(l, s);
			if (advance < 0)
				goto trunc;
			s += advance;

			/* aliases */
			na = ntohs(gabn->naliases);
			for (i = 0; i < na; i++) {
				advance = lwres_printnamelen(s);
				if (advance < 0)
					goto trunc;
				s += advance;
			}

			/* addrs */
			na = ntohs(gabn->naddrs);
			for (i = 0; i < na; i++) {
				advance = lwres_printaddr((lwres_addr_t *)s);
				if (advance < 0)
					goto trunc;
				s += advance;
			}
			break;
		case LWRES_OPCODE_GETNAMEBYADDR:
			gnba = (lwres_gnbaresponse_t *)(np + 1);
			TCHECK(gnba->realnamelen);
			/* XXX gnba points to packed struct */
			s = (const char *)&gnba->realnamelen +
			    sizeof(gnba->realnamelen);
			l = ntohs(gnba->realnamelen);

			/* BIND910: not used */
			if (vflag > 2) {
				printf(" flags:0x%lx",
				    (unsigned long)ntohl(gnba->flags));
			}

			printf(" %u", ntohs(gnba->naliases));

			advance = lwres_printname(l, s);
			if (advance < 0)
				goto trunc;
			s += advance;

			/* aliases */
			na = ntohs(gnba->naliases);
			for (i = 0; i < na; i++) {
				advance = lwres_printnamelen(s);
				if (advance < 0)
					goto trunc;
				s += advance;
			}
			break;
		case LWRES_OPCODE_GETRDATABYNAME:
			/* XXX no trace, not tested */
			grbn = (lwres_grbnresponse_t *)(np + 1);
			TCHECK(grbn->nsigs);

			/* BIND910: not used */
			if (vflag > 2) {
				printf(" flags:0x%lx",
				    (unsigned long)ntohl(grbn->flags));
			}

			printf(" %s", tok2str(ns_type2str, "Type%d",
			    ntohs(grbn->rdtype)));
			if (ntohs(grbn->rdclass) != C_IN)
				printf(" %s", tok2str(ns_class2str, "Class%d",
				    ntohs(grbn->rdclass)));
			printf(" TTL ");
			relts_print(ntohl(grbn->ttl));
			printf(" %u/%u", ntohs(grbn->nrdatas),
			    ntohs(grbn->nsigs));

			/* XXX grbn points to packed struct */
			s = (const char *)&grbn->nsigs+ sizeof(grbn->nsigs);

			advance = lwres_printnamelen(s);
			if (advance < 0)
				goto trunc;
			s += advance;

			/* rdatas */
			na = ntohs(grbn->nrdatas);
			if (na > 0)
			  printf(" ");

			for (i = 0; i < na; i++) {
				/* XXX should decode resource data */
				advance = lwres_printb64len(s);
				if (advance < 0)
					goto trunc;
				s += advance;
			}

			/* sigs */
			na = ntohs(grbn->nsigs);
			if (na > 0)
			  printf(" ");

			for (i = 0; i < na; i++) {
				/* XXX how should we print it? */
				advance = lwres_printb64len(s);
				if (advance < 0)
					goto trunc;
				s += advance;
			}
			break;
		default:
			unsupported++;
			break;
		}
	}

  tail:
	/* length mismatch */
	if (ntohl(np->length) != length) {
		printf(" [len: %lu != %u]", (unsigned long)ntohl(np->length),
		    length);
	}
	if (!unsupported && s < (const char *)np + ntohl(np->length))
		printf("[extra]");
	return;

  trunc:
	printf("[|lwres]");
	return;
}
