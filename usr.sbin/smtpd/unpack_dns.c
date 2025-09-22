/*	$OpenBSD: unpack_dns.c,v 1.3 2022/01/20 14:18:10 naddy Exp $	*/

/*
 * Copyright (c) 2011-2014 Eric Faurot <eric@faurot.net>
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

#include <string.h>

#include "unpack_dns.h"

static int unpack_data(struct unpack *, void *, size_t);
static int unpack_u16(struct unpack *, uint16_t *);
static int unpack_u32(struct unpack *, uint32_t *);
static int unpack_inaddr(struct unpack *, struct in_addr *);
static int unpack_in6addr(struct unpack *, struct in6_addr *);
static int unpack_dname(struct unpack *, char *, size_t);

void
unpack_init(struct unpack *unpack, const char *buf, size_t len)
{
	unpack->buf = buf;
	unpack->len = len;
	unpack->offset = 0;
	unpack->err = NULL;
}

int
unpack_header(struct unpack *p, struct dns_header *h)
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
unpack_query(struct unpack *p, struct dns_query *q)
{
	unpack_dname(p, q->q_dname, sizeof(q->q_dname));
	unpack_u16(p, &q->q_type);
	unpack_u16(p, &q->q_class);

	return (p->err) ? (-1) : (0);
}

int
unpack_rr(struct unpack *p, struct dns_rr *rr)
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
		p->err = "bad dlen";

	return (p->err) ? (-1) : (0);
}

ssize_t
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
			if (offset + 2 > len)
				return (-1);
			ptr = 256 * (n & ~0xc0) + data[offset + 1];
			if (ptr >= start)
				return (-1);
			if (end < offset + 2)
				end = offset + 2;
			offset = start = ptr;
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

char *
print_dname(const char *_dname, char *buf, size_t max)
{
	const unsigned char *dname = _dname;
	char    *res;
	size_t   left, count;

	if (_dname[0] == 0) {
		(void)strlcpy(buf, ".", max);
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

static int
unpack_data(struct unpack *p, void *data, size_t len)
{
	if (p->err)
		return (-1);

	if (p->len - p->offset < len) {
		p->err = "too short";
		return (-1);
	}

	memmove(data, p->buf + p->offset, len);
	p->offset += len;

	return (0);
}

static int
unpack_u16(struct unpack *p, uint16_t *u16)
{
	if (unpack_data(p, u16, 2) == -1)
		return (-1);

	*u16 = ntohs(*u16);

	return (0);
}

static int
unpack_u32(struct unpack *p, uint32_t *u32)
{
	if (unpack_data(p, u32, 4) == -1)
		return (-1);

	*u32 = ntohl(*u32);

	return (0);
}

static int
unpack_inaddr(struct unpack *p, struct in_addr *a)
{
	return (unpack_data(p, a, 4));
}

static int
unpack_in6addr(struct unpack *p, struct in6_addr *a6)
{
	return (unpack_data(p, a6, 16));
}

static int
unpack_dname(struct unpack *p, char *dst, size_t max)
{
	ssize_t e;

	if (p->err)
		return (-1);

	e = dname_expand(p->buf, p->len, p->offset, &p->offset, dst, max);
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
