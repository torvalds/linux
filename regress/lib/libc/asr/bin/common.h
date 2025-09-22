/*	$OpenBSD: common.h,v 1.2 2018/12/15 15:16:12 eric Exp $	*/
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

#include <arpa/nameser.h>

#include <netinet/in.h>

#include <netdb.h>


#define unpack_rr		__unpack_rr
#define unpack_header		__unpack_header
#define packed_init		__packed_init
#define unpack_query		__unpack_query
#define sockaddr_from_str	__sockaddr_from_str
#define print_addr		__print_addr

extern int long_err;
extern int gai_errno;
extern int rrset_errno;

const char *classtostr(uint16_t);
const char *typetostr(uint16_t);
const char *rcodetostr(uint16_t);

uint16_t strtotype(const char*);
uint16_t strtoclass(const char*);
int strtoresopt(const char*);
void parseresopt(const char*);

void	print_rrsetinfo(struct rrsetinfo *);
void	print_addrinfo(struct addrinfo *);
void	print_errors(void);
void	print_hostent(struct hostent *);
void	print_netent(struct netent *);

int	sockaddr_from_str(struct sockaddr *, int, const char *);
int	addr_from_str(char *, int *, int *, const char *);
char*	gethostarg(char *);

#define QR_MASK		(0x1 << 15)
#define OPCODE_MASK	(0xf << 11)
#define AA_MASK		(0x1 << 10)
#define TC_MASK		(0x1 <<  9)
#define RD_MASK		(0x1 <<  8)
#define RA_MASK		(0x1 <<  7)
#define Z_MASK		(0x7 <<  4)
#define RCODE_MASK	(0xf)

#define OPCODE(v)	((v) & OPCODE_MASK)
#define RCODE(v)	((v) & RCODE_MASK)


struct packed {
	char		*data;
	size_t		 len;
	size_t		 offset;
	const char	*err;
};

struct header {
	uint16_t	id;
	uint16_t	flags;
	uint16_t	qdcount;
	uint16_t	ancount;
	uint16_t	nscount;
	uint16_t	arcount;
};

struct query {
	char		q_dname[MAXDNAME];
	uint16_t	q_type;
	uint16_t	q_class;
};

struct rr {
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

void	packed_init(struct packed*, char*, size_t);
int	pack_header(struct packed*, const struct header*);
int	pack_query(struct packed*, uint16_t, uint16_t, const char*);
int	unpack_header(struct packed*, struct header*);
int	unpack_query(struct packed*, struct query*);
int	unpack_rr(struct packed*, struct rr*);
