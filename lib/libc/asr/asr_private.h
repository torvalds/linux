/*	$OpenBSD: asr_private.h,v 1.49 2023/11/20 12:15:16 florian Exp $	*/
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

#include <stdio.h>

#define QR_MASK		(0x1 << 15)
#define OPCODE_MASK	(0xf << 11)
#define AA_MASK		(0x1 << 10)
#define TC_MASK		(0x1 <<  9)
#define RD_MASK		(0x1 <<  8)
#define RA_MASK		(0x1 <<  7)
#define Z_MASK		(0x1 <<  6)
#define AD_MASK		(0x1 <<  5)
#define CD_MASK		(0x1 <<  4)
#define RCODE_MASK	(0xf)

#define OPCODE(v)	((v) & OPCODE_MASK)
#define RCODE(v)	((v) & RCODE_MASK)


struct asr_pack {
	char		*buf;
	size_t		 len;
	size_t		 offset;
	int		 err;
};

struct asr_unpack {
	const char	*buf;
	size_t		 len;
	size_t		 offset;
	int		 err;
};

struct asr_dns_header {
	uint16_t	id;
	uint16_t	flags;
	uint16_t	qdcount;
	uint16_t	ancount;
	uint16_t	nscount;
	uint16_t	arcount;
};

struct asr_dns_query {
	char		q_dname[MAXDNAME];
	uint16_t	q_type;
	uint16_t	q_class;
};

struct asr_dns_rr {
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


#define ASR_MAXNS	5
#define ASR_MAXDB	3
#define ASR_MAXDOM	10

enum async_type {
	ASR_SEND,
	ASR_SEARCH,
	ASR_GETRRSETBYNAME,
	ASR_GETHOSTBYNAME,
	ASR_GETHOSTBYADDR,
	ASR_GETADDRINFO,
	ASR_GETNAMEINFO,
};

#define	ASR_DB_FILE	'f'
#define	ASR_DB_DNS	'b'

struct asr_ctx {
	int		 ac_refcount;
	int		 ac_options;
	int		 ac_ndots;
	char		*ac_domain;
	int		 ac_domcount;
	char		*ac_dom[ASR_MAXDOM];
	int		 ac_dbcount;
	char		 ac_db[ASR_MAXDB + 1];
	int		 ac_family[3];

	int		 ac_nscount;
	int		 ac_nstimeout;
	int		 ac_nsretries;
	struct sockaddr *ac_ns[ASR_MAXNS];

};

struct asr {
	pid_t		 a_pid;
	time_t		 a_mtime;
	time_t		 a_rtime;
	struct asr_ctx	*a_ctx;
};

#define ASYNC_COND		0
#define ASYNC_DONE		1

#define	ASYNC_DOM_FQDN		0x00000001
#define	ASYNC_DOM_NDOTS		0x00000002
#define	ASYNC_DOM_DOMAIN	0x00000004
#define ASYNC_DOM_ASIS		0x00000008

#define	ASYNC_NODATA		0x00000100
#define	ASYNC_AGAIN		0x00000200

#define	ASYNC_GETNET		0x00001000
#define	ASYNC_EXTOBUF		0x00002000

#define	ASYNC_NO_INET		0x00010000
#define	ASYNC_NO_INET6		0x00020000

struct asr_query {
	int		(*as_run)(struct asr_query *, struct asr_result *);
	struct asr_ctx	*as_ctx;
	int		 as_type;
	int		 as_flags;
	int		 as_state;

	/* cond */
	int		 as_timeout;
	int		 as_fd;
	struct asr_query *as_subq;

	/* loop indices in ctx */
	int		 as_dom_step;
	int		 as_dom_idx;
	int		 as_dom_flags;
	int		 as_family_idx;
	int		 as_db_idx;

	int		 as_count;

	union {
		struct {
			uint16_t	 reqid;
			int		 class;
			int		 type;
			char		*dname;		/* not fqdn! */
			int		 rcode;		/* response code */
			int		 ancount;	/* answer count */

			int		 nsidx;
			int		 nsloop;

			/* io buffers for query/response */
			unsigned char	*obuf;
			size_t		 obuflen;
			size_t		 obufsize;
			unsigned char	*ibuf;
			size_t		 ibuflen;
			size_t		 ibufsize;
			size_t		 datalen; /* for tcp io */
			uint16_t	 pktlen;
		} dns;

		struct {
			int		 class;
			int		 type;
			char		*name;
			int		 saved_h_errno;
		} search;

		struct {
			int		 flags;
			int		 class;
			int		 type;
			char		*name;
		} rrset;

		struct {
			char		*name;
			int		 family;
			char		 addr[16];
			int		 addrlen;
			int		 subq_h_errno;
		} hostnamadr;

		struct {
			char		*hostname;
			char		*servname;
			int		 port_tcp;
			int		 port_udp;
			union {
				struct sockaddr		sa;
				struct sockaddr_in	sain;
				struct sockaddr_in6	sain6;
			}		 sa;

			struct addrinfo	 hints;
			char		*fqdn;
			struct addrinfo	*aifirst;
			struct addrinfo	*ailast;
		} ai;

		struct {
			char		*hostname;
			char		*servname;
			size_t		 hostnamelen;
			size_t		 servnamelen;
			union {
				struct sockaddr		sa;
				struct sockaddr_in	sain;
				struct sockaddr_in6	sain6;
			}		 sa;
			int		 flags;
		} ni;
#define MAXTOKEN 10
	} as;

};

#define AS_DB(p) ((p)->as_ctx->ac_db[(p)->as_db_idx - 1])
#define AS_FAMILY(p) ((p)->as_ctx->ac_family[(p)->as_family_idx])

enum asr_state {
	ASR_STATE_INIT,
	ASR_STATE_NEXT_DOMAIN,
	ASR_STATE_NEXT_DB,
	ASR_STATE_SAME_DB,
	ASR_STATE_NEXT_FAMILY,
	ASR_STATE_NEXT_NS,
	ASR_STATE_UDP_SEND,
	ASR_STATE_UDP_RECV,
	ASR_STATE_TCP_WRITE,
	ASR_STATE_TCP_READ,
	ASR_STATE_PACKET,
	ASR_STATE_SUBQUERY,
	ASR_STATE_NOT_FOUND,
	ASR_STATE_HALT,
};

#define MAXPACKETSZ	4096

__BEGIN_HIDDEN_DECLS

/* asr_utils.c */
void _asr_pack_init(struct asr_pack *, char *, size_t);
int _asr_pack_header(struct asr_pack *, const struct asr_dns_header *);
int _asr_pack_query(struct asr_pack *, uint16_t, uint16_t, const char *);
int _asr_pack_edns0(struct asr_pack *, uint16_t, int);
void _asr_unpack_init(struct asr_unpack *, const char *, size_t);
int _asr_unpack_header(struct asr_unpack *, struct asr_dns_header *);
int _asr_unpack_query(struct asr_unpack *, struct asr_dns_query *);
int _asr_unpack_rr(struct asr_unpack *, struct asr_dns_rr *);
int _asr_sockaddr_from_str(struct sockaddr *, int, const char *);
ssize_t _asr_dname_from_fqdn(const char *, char *, size_t);
ssize_t _asr_addr_as_fqdn(const char *, int, char *, size_t);
int hnok_lenient(const char *);
int _asr_is_localhost(const char*);

/* asr.c */
void _asr_resolver_done(void *);
struct asr_ctx *_asr_use_resolver(void *);
struct asr_ctx *_asr_no_resolver(void);
void _asr_ctx_unref(struct asr_ctx *);
struct asr_query *_asr_async_new(struct asr_ctx *, int);
void _asr_async_free(struct asr_query *);
size_t _asr_make_fqdn(const char *, const char *, char *, size_t);
char *_asr_strdname(const char *, char *, size_t);
int _asr_iter_db(struct asr_query *);
int _asr_parse_namedb_line(FILE *, char **, int, char *, size_t);

/* *_async.c */
struct asr_query *_res_query_async_ctx(const char *, int, int, struct asr_ctx *);
struct asr_query *_res_search_async_ctx(const char *, int, int, struct asr_ctx *);
struct asr_query *_gethostbyaddr_async_ctx(const void *, socklen_t, int,
    struct asr_ctx *);

int _asr_iter_domain(struct asr_query *, const char *, char *, size_t);

#ifdef DEBUG

#define DPRINT(...)		do { if(_asr_debug) {		\
		fprintf(_asr_debug, __VA_ARGS__);		\
	} } while (0)
#define DPRINT_PACKET(n, p, s)	do { if(_asr_debug) {		\
		fprintf(_asr_debug, "----- %s -----\n", n);	\
		_asr_dump_packet(_asr_debug, (p), (s));		\
		fprintf(_asr_debug, "--------------\n");		\
	} } while (0)

#else /* DEBUG */

#define DPRINT(...)
#define DPRINT_PACKET(...)

#endif /* DEBUG */

const char *_asr_querystr(int);
const char *_asr_statestr(int);
const char *_asr_transitionstr(int);
const char *_asr_print_sockaddr(const struct sockaddr *, char *, size_t);
void _asr_dump_config(FILE *, struct asr *);
void _asr_dump_packet(FILE *, const void *, size_t);

extern FILE *_asr_debug;

#define async_set_state(a, s) do {		\
	DPRINT("asr: [%s@%p] %s -> %s\n",	\
		_asr_querystr((a)->as_type),	\
		as,				\
		_asr_statestr((a)->as_state),	\
		_asr_statestr((s)));		\
	(a)->as_state = (s); } while (0)

__END_HIDDEN_DECLS
