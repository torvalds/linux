/*	$OpenBSD: asr_utils.c,v 1.22 2023/11/20 12:15:16 florian Exp $	*/
/*
 * Copyright (c) 2009-2012	Eric Faurot	<eric@faurot.net>
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
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <netdb.h>

#include <asr.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "asr_private.h"

static int dname_check_label(const char *, size_t);
static ssize_t dname_expand(const unsigned char *, size_t, size_t, size_t *,
    char *, size_t);

static int unpack_data(struct asr_unpack *, void *, size_t);
static int unpack_u16(struct asr_unpack *, uint16_t *);
static int unpack_u32(struct asr_unpack *, uint32_t *);
static int unpack_inaddr(struct asr_unpack *, struct in_addr *);
static int unpack_in6addr(struct asr_unpack *, struct in6_addr *);
static int unpack_dname(struct asr_unpack *, char *, size_t);

static int pack_data(struct asr_pack *, const void *, size_t);
static int pack_u16(struct asr_pack *, uint16_t);
static int pack_dname(struct asr_pack *, const char *);

static int
dname_check_label(const char *s, size_t l)
{
	if (l == 0 || l > 63)
		return (-1);

	return (0);
}

ssize_t
_asr_dname_from_fqdn(const char *str, char *dst, size_t max)
{
	ssize_t	 res;
	size_t	 l, n;
	char	*d;

	res = 0;

	/* special case: the root domain */
	if (str[0] == '.') {
		if (str[1] != '\0')
			return (-1);
		if (dst && max >= 1)
			*dst = '\0';
		return (1);
	}

	for (; *str; str = d + 1) {

		d = strchr(str, '.');
		if (d == NULL || d == str)
			return (-1);

		l = (d - str);

		if (dname_check_label(str, l) == -1)
			return (-1);

		res += l + 1;

		if (dst) {
			*dst++ = l;
			max -= 1;
			n = (l > max) ? max : l;
			memmove(dst, str, n);
			max -= n;
			if (max == 0)
				dst = NULL;
			else
				dst += n;
		}
	}

	if (dst)
		*dst++ = '\0';

	return (res + 1);
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

	for (; (n = data[offset]); ) {
		if ((n & 0xc0) == 0xc0) {
			if (offset + 1 >= len)
				return (-1);
			ptr = 256 * (n & ~0xc0) + data[offset + 1];
			if (ptr >= start)
				return (-1);
			if (end < offset + 2)
				end = offset + 2;
			offset = start = ptr;
			continue;
		}
		if (offset + n + 1 >= len)
			return (-1);

		if (dname_check_label(data + offset + 1, n) == -1)
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

void
_asr_pack_init(struct asr_pack *pack, char *buf, size_t len)
{
	pack->buf = buf;
	pack->len = len;
	pack->offset = 0;
	pack->err = 0;
}

void
_asr_unpack_init(struct asr_unpack *unpack, const char *buf, size_t len)
{
	unpack->buf = buf;
	unpack->len = len;
	unpack->offset = 0;
	unpack->err = 0;
}

static int
unpack_data(struct asr_unpack *p, void *data, size_t len)
{
	if (p->err)
		return (-1);

	if (p->len - p->offset < len) {
		p->err = EOVERFLOW;
		return (-1);
	}

	memmove(data, p->buf + p->offset, len);
	p->offset += len;

	return (0);
}

static int
unpack_u16(struct asr_unpack *p, uint16_t *u16)
{
	if (unpack_data(p, u16, 2) == -1)
		return (-1);

	*u16 = ntohs(*u16);

	return (0);
}

static int
unpack_u32(struct asr_unpack *p, uint32_t *u32)
{
	if (unpack_data(p, u32, 4) == -1)
		return (-1);

	*u32 = ntohl(*u32);

	return (0);
}

static int
unpack_inaddr(struct asr_unpack *p, struct in_addr *a)
{
	return (unpack_data(p, a, 4));
}

static int
unpack_in6addr(struct asr_unpack *p, struct in6_addr *a6)
{
	return (unpack_data(p, a6, 16));
}

static int
unpack_dname(struct asr_unpack *p, char *dst, size_t max)
{
	ssize_t e;

	if (p->err)
		return (-1);

	e = dname_expand(p->buf, p->len, p->offset, &p->offset, dst, max);
	if (e == -1) {
		p->err = EINVAL;
		return (-1);
	}
	if (e < 0 || e > MAXDNAME) {
		p->err = ERANGE;
		return (-1);
	}

	return (0);
}

int
_asr_unpack_header(struct asr_unpack *p, struct asr_dns_header *h)
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
_asr_unpack_query(struct asr_unpack *p, struct asr_dns_query *q)
{
	unpack_dname(p, q->q_dname, sizeof(q->q_dname));
	unpack_u16(p, &q->q_type);
	unpack_u16(p, &q->q_class);

	return (p->err) ? (-1) : (0);
}

int
_asr_unpack_rr(struct asr_unpack *p, struct asr_dns_rr *rr)
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
		p->err = EOVERFLOW;
		return (-1);
	}

	save_offset = p->offset;

	switch (rr->rr_type) {

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
		rr->rr.other.rdata = p->buf + p->offset;
		rr->rr.other.rdlen = rdlen;
		p->offset += rdlen;
	}

	if (p->err)
		return (-1);

	/* make sure that the advertised rdlen is really ok */
	if (p->offset - save_offset != rdlen)
		p->err = EINVAL;

	return (p->err) ? (-1) : (0);
}

static int
pack_data(struct asr_pack *p, const void *data, size_t len)
{
	if (p->err)
		return (-1);

	if (p->len < p->offset + len) {
		p->err = EOVERFLOW;
		return (-1);
	}

	memmove(p->buf + p->offset, data, len);
	p->offset += len;

	return (0);
}

static int
pack_u16(struct asr_pack *p, uint16_t v)
{
	v = htons(v);

	return (pack_data(p, &v, 2));
}

static int
pack_dname(struct asr_pack *p, const char *dname)
{
	/* dname compression would be nice to have here.
	 * need additional context.
	 */
	return (pack_data(p, dname, strlen(dname) + 1));
}

int
_asr_pack_header(struct asr_pack *p, const struct asr_dns_header *h)
{
	struct asr_dns_header c;

	c.id = h->id;
	c.flags = htons(h->flags);
	c.qdcount = htons(h->qdcount);
	c.ancount = htons(h->ancount);
	c.nscount = htons(h->nscount);
	c.arcount = htons(h->arcount);

	return (pack_data(p, &c, HFIXEDSZ));
}

int
_asr_pack_query(struct asr_pack *p, uint16_t type, uint16_t class, const char *dname)
{
	pack_dname(p, dname);
	pack_u16(p, type);
	pack_u16(p, class);

	return (p->err) ? (-1) : (0);
}

int
_asr_pack_edns0(struct asr_pack *p, uint16_t pktsz, int dnssec_do)
{
	DPRINT("asr EDNS0 pktsz:%hu dnssec:%s\n", pktsz,
	    dnssec_do ? "yes" : "no");

	pack_dname(p, "");	/* root */
	pack_u16(p, T_OPT);	/* OPT */
	pack_u16(p, pktsz);	/* UDP payload size */

	/* extended RCODE and flags */
	pack_u16(p, 0);
	pack_u16(p, dnssec_do ? DNS_MESSAGEEXTFLAG_DO : 0);

	pack_u16(p, 0);		/* RDATA len */

	return (p->err) ? (-1) : (0);
}

int
_asr_sockaddr_from_str(struct sockaddr *sa, int family, const char *str)
{
	struct in_addr		 ina;
	struct in6_addr		 in6a;
	struct sockaddr_in	*sin;
	struct sockaddr_in6	*sin6;
	char			*cp, *str2;
	const char		*errstr;

	switch (family) {
	case PF_UNSPEC:
		if (_asr_sockaddr_from_str(sa, PF_INET, str) == 0)
			return (0);
		return _asr_sockaddr_from_str(sa, PF_INET6, str);

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
		cp = strchr(str, SCOPE_DELIMITER);
		if (cp) {
			str2 = strdup(str);
			if (str2 == NULL)
				return (-1);
			str2[cp - str] = '\0';
			if (inet_pton(PF_INET6, str2, &in6a) != 1) {
				free(str2);
				return (-1);
			}
			cp++;
			free(str2);
		} else if (inet_pton(PF_INET6, str, &in6a) != 1)
			return (-1);

		sin6 = (struct sockaddr_in6 *)sa;
		memset(sin6, 0, sizeof *sin6);
		sin6->sin6_len = sizeof(struct sockaddr_in6);
		sin6->sin6_family = PF_INET6;
		sin6->sin6_addr = in6a;

		if (cp == NULL)
			return (0);

		if (IN6_IS_ADDR_LINKLOCAL(&in6a) ||
		    IN6_IS_ADDR_MC_LINKLOCAL(&in6a) ||
		    IN6_IS_ADDR_MC_INTFACELOCAL(&in6a))
			if ((sin6->sin6_scope_id = if_nametoindex(cp)))
				return (0);

		sin6->sin6_scope_id = strtonum(cp, 0, UINT32_MAX, &errstr);
		if (errstr)
			return (-1);
		return (0);

	default:
		break;
	}

	return (-1);
}

ssize_t
_asr_addr_as_fqdn(const char *addr, int family, char *dst, size_t max)
{
	const struct in6_addr	*in6_addr;
	in_addr_t		 in_addr;

	switch (family) {
	case AF_INET:
		in_addr = ntohl(*((const in_addr_t *)addr));
		snprintf(dst, max,
		    "%d.%d.%d.%d.in-addr.arpa.",
		    in_addr & 0xff,
		    (in_addr >> 8) & 0xff,
		    (in_addr >> 16) & 0xff,
		    (in_addr >> 24) & 0xff);
		break;
	case AF_INET6:
		in6_addr = (const struct in6_addr *)addr;
		snprintf(dst, max,
		    "%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x."
		    "%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x."
		    "ip6.arpa.",
		    in6_addr->s6_addr[15] & 0xf,
		    (in6_addr->s6_addr[15] >> 4) & 0xf,
		    in6_addr->s6_addr[14] & 0xf,
		    (in6_addr->s6_addr[14] >> 4) & 0xf,
		    in6_addr->s6_addr[13] & 0xf,
		    (in6_addr->s6_addr[13] >> 4) & 0xf,
		    in6_addr->s6_addr[12] & 0xf,
		    (in6_addr->s6_addr[12] >> 4) & 0xf,
		    in6_addr->s6_addr[11] & 0xf,
		    (in6_addr->s6_addr[11] >> 4) & 0xf,
		    in6_addr->s6_addr[10] & 0xf,
		    (in6_addr->s6_addr[10] >> 4) & 0xf,
		    in6_addr->s6_addr[9] & 0xf,
		    (in6_addr->s6_addr[9] >> 4) & 0xf,
		    in6_addr->s6_addr[8] & 0xf,
		    (in6_addr->s6_addr[8] >> 4) & 0xf,
		    in6_addr->s6_addr[7] & 0xf,
		    (in6_addr->s6_addr[7] >> 4) & 0xf,
		    in6_addr->s6_addr[6] & 0xf,
		    (in6_addr->s6_addr[6] >> 4) & 0xf,
		    in6_addr->s6_addr[5] & 0xf,
		    (in6_addr->s6_addr[5] >> 4) & 0xf,
		    in6_addr->s6_addr[4] & 0xf,
		    (in6_addr->s6_addr[4] >> 4) & 0xf,
		    in6_addr->s6_addr[3] & 0xf,
		    (in6_addr->s6_addr[3] >> 4) & 0xf,
		    in6_addr->s6_addr[2] & 0xf,
		    (in6_addr->s6_addr[2] >> 4) & 0xf,
		    in6_addr->s6_addr[1] & 0xf,
		    (in6_addr->s6_addr[1] >> 4) & 0xf,
		    in6_addr->s6_addr[0] & 0xf,
		    (in6_addr->s6_addr[0] >> 4) & 0xf);
		break;
	default:
		return (-1);
	}
	return (0);
}

int
hnok_lenient(const char *dn)
{
	int pch = '\0', ch = *dn++;

	while (ch != '\0') {
		/* can't start with . or - */
		if (pch == '\0' && (ch == '.' || ch == '-'))
			return 0;
		if (pch == '.' && ch == '.')
			return 0;
		if (!(isalpha((unsigned char)ch) || isdigit((unsigned char)ch) ||
		    ch == '.' || ch == '-' || ch == '_'))
			return 0;
		pch = ch; ch = *dn++;
	}
	return 1;
}

/* Check if the hostname is localhost or if it's in the localhost domain */
int
_asr_is_localhost(const char *hn)
{
	size_t	 hnlen, localhostlen;

	if (hn == NULL)
		return 0;

	if (strcasecmp(hn, "localhost") == 0 ||
	    strcasecmp(hn, "localhost.") == 0)
		return 1;

	hnlen = strlen(hn);
	localhostlen = strlen(".localhost");

	if (hnlen < localhostlen)
		return 0;

	if (strcasecmp(hn + hnlen - localhostlen, ".localhost") == 0)
		return 1;

	localhostlen++;
	if (hnlen < localhostlen)
		return 0;

	if (strcasecmp(hn + hnlen - localhostlen, ".localhost.") == 0)
		return 1;

	return 0;
}
