/*	$OpenBSD: unpack_dns.h,v 1.2 2021/06/14 17:58:16 eric Exp $	*/

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

#include <sys/types.h>

#include <netinet/in.h>

#include <arpa/nameser.h>

struct unpack {
	const char	*buf;
	size_t		 len;
	size_t		 offset;
	const char	*err;
};

struct dns_header {
	uint16_t	id;
	uint16_t	flags;
	uint16_t	qdcount;
	uint16_t	ancount;
	uint16_t	nscount;
	uint16_t	arcount;
};

struct dns_query {
	char		q_dname[MAXDNAME];
	uint16_t	q_type;
	uint16_t	q_class;
};

struct dns_rr {
	char		rr_dname[MAXDNAME];
	uint16_t	rr_type;
	uint16_t	rr_class;
	uint32_t	rr_ttl;
	union {
		struct {
			char	cname[MAXDNAME];
		} cname;
		struct {
			uint16_t	preference;
			char		exchange[MAXDNAME];
		} mx;
		struct {
			char	nsname[MAXDNAME];
		} ns;
		struct {
			char	ptrname[MAXDNAME];
		} ptr;
		struct {
			char		mname[MAXDNAME];
			char		rname[MAXDNAME];
			uint32_t	serial;
			uint32_t	refresh;
			uint32_t	retry;
			uint32_t	expire;
			uint32_t	minimum;
		} soa;
		struct {
			struct in_addr	addr;
		} in_a;
		struct {
			struct in6_addr	addr6;
		} in_aaaa;
		struct {
			uint16_t	 rdlen;
			const void	*rdata;
		} other;
	} rr;
};

void	 unpack_init(struct unpack *, const char *, size_t);
int	 unpack_header(struct unpack *, struct dns_header *);
int	 unpack_rr(struct unpack *, struct dns_rr *);
int	 unpack_query(struct unpack *, struct dns_query *);
char    *print_dname(const char *, char *, size_t);
ssize_t	 dname_expand(const unsigned char *, size_t, size_t, size_t *,
	    char *, size_t);

