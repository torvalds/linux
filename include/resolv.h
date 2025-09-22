/*	$OpenBSD: resolv.h,v 1.23 2021/11/22 20:18:27 jca Exp $	*/

/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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

/*
 * ++Copyright++ 1983, 1987, 1989, 1993
 * -
 * Copyright (c) 1983, 1987, 1989, 1993
 *    The Regents of the University of California.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * -
 * Portions Copyright (c) 1993 by Digital Equipment Corporation.
 * 
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies, and that
 * the name of Digital Equipment Corporation not be used in advertising or
 * publicity pertaining to distribution of the document or software without
 * specific, written prior permission.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND DIGITAL EQUIPMENT CORP. DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS.   IN NO EVENT SHALL DIGITAL EQUIPMENT
 * CORPORATION BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 * -
 * --Copyright--
 */

/*
 *	@(#)resolv.h	8.1 (Berkeley) 6/2/93
 *	$From: resolv.h,v 8.17 1996/11/26 10:11:20 vixie Exp $
 */

#ifndef _RESOLV_H_
#define	_RESOLV_H_

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>

/*
 * Revision information.  This is the release date in YYYYMMDD format.
 * It can change every day so the right thing to do with it is use it
 * in preprocessor commands such as "#if (__RES > 19931104)".  Do not
 * compare for equality; rather, use it to determine whether your resolver
 * is new enough to contain a certain feature.
 */

#define	__RES	19960801

/*
 * Resolver configuration file.
 * Normally not present, but may contain the address of the
 * initial name server(s) to query and the domain search list.
 */

#ifndef _PATH_RESCONF
#define _PATH_RESCONF        "/etc/resolv.conf"
#endif

/*
 * Global defines and variables for resolver stub.
 */
#define	MAXNS			3	/* max # name servers we'll track */
#define	MAXDFLSRCH		3	/* # default domain levels to try */
#define	MAXDNSRCH		6	/* max # domains in search path */
#define	LOCALDOMAINPARTS	2	/* min levels in name that is "local" */
#define MAXDNSLUS		4	/* max # of host lookup types */

#define	RES_TIMEOUT		5	/* min. seconds between retries */
#define	MAXRESOLVSORT		10	/* number of net to sort on */
#define	RES_MAXNDOTS		15	/* should reflect bit field size */

struct __res_state {
	int	retrans;	 	/* retransmission time interval */
	int	retry;			/* number of times to retransmit */
	unsigned int	options;	/* option flags - see below. */
	int	nscount;		/* number of name servers */
	int	family[2];		/* specifies which address
					 * families will be queried and
					 * in which order */
	struct sockaddr_in
		nsaddr_list[MAXNS];	/* address of name server */
#define	nsaddr	nsaddr_list[0]		/* for backward compatibility */
	unsigned short	id;		/* current message id */
	char	*dnsrch[MAXDNSRCH+1];	/* components of domain to search */
	char	defdname[256];		/* default domain (deprecated) */
	unsigned int	pfcode;		/* RES_PRF_ flags - see below. */
	unsigned ndots:4;		/* threshold for initial abs. query */
	unsigned nsort:4;		/* number of elements in sort_list[] */
	char	unused[3];
	struct {
		struct in_addr	addr;
		u_int32_t	mask;
	} sort_list[MAXRESOLVSORT];
	char    lookups[MAXDNSLUS];
	struct { time_t __res_sec; long __res_nsec; } restimespec;
	time_t	reschktime;
};

#if 1 /* INET6 */
/*
 * replacement of __res_state, separated to keep binary compatibility.
 */
struct __res_state_ext {
	struct sockaddr_storage nsaddr_list[MAXNS];
	struct {
		int	af;		/* address family for addr, mask */
		union {
			struct in_addr ina;
			struct in6_addr in6a;
		} addr, mask;
	} sort_list[MAXRESOLVSORT];
};
#endif


/*
 * Resolver options (keep these in synch with res_debug.c, please)
 */
#define RES_INIT	0x00000001	/* address initialized */
#define RES_DEBUG	0x00000002	/* print debug messages */
#define RES_AAONLY	0x00000004	/* authoritative answers only (!IMPL)*/
#define RES_USEVC	0x00000008	/* use virtual circuit */
#define RES_PRIMARY	0x00000010	/* query primary server only (!IMPL) */
#define RES_IGNTC	0x00000020	/* ignore trucation errors */
#define RES_RECURSE	0x00000040	/* recursion desired */
#define RES_DEFNAMES	0x00000080	/* use default domain name */
#define RES_STAYOPEN	0x00000100	/* Keep TCP socket open */
#define RES_DNSRCH	0x00000200	/* search up local domain tree */
#define	RES_INSECURE1	0x00000400	/* type 1 security disabled */
#define	RES_INSECURE2	0x00000800	/* type 2 security disabled */
#define	RES_NOALIASES	0x00001000	/* shuts off HOSTALIASES feature */
#define	RES_USE_INET6	0x00002000	/* use/map IPv6 in gethostbyname() */
/* KAME extensions: use higher bit to avoid conflict with ISC use */
#define	RES_USE_EDNS0	0x40000000	/* use EDNS0 */
/* DNSSEC extensions: use higher bit to avoid conflict with ISC use */
#define	RES_USE_DNSSEC	0x20000000	/* use DNSSEC using OK bit in OPT */
#define	RES_USE_CD	0x10000000	/* set Checking Disabled flag */
#define	RES_TRUSTAD	0x80000000	/* Request AD, keep it in responses. */

#define RES_DEFAULT	(RES_RECURSE | RES_DEFNAMES | RES_DNSRCH)

/*
 * Resolver "pfcode" values.  Used by dig.
 */
#define RES_PRF_STATS	0x00000001
/*			0x00000002	*/
#define RES_PRF_CLASS   0x00000004
#define RES_PRF_CMD	0x00000008
#define RES_PRF_QUES	0x00000010
#define RES_PRF_ANS	0x00000020
#define RES_PRF_AUTH	0x00000040
#define RES_PRF_ADD	0x00000080
#define RES_PRF_HEAD1	0x00000100
#define RES_PRF_HEAD2	0x00000200
#define RES_PRF_TTLID	0x00000400
#define RES_PRF_HEADX	0x00000800
#define RES_PRF_QUERY	0x00001000
#define RES_PRF_REPLY	0x00002000
#define RES_PRF_INIT    0x00004000
/*			0x00008000	*/

/* hooks are still experimental as of 4.9.2 */
typedef enum { res_goahead, res_nextns, res_modified, res_done, res_error }
	res_sendhookact;

typedef res_sendhookact (*res_send_qhook)(struct sockaddr_in * const *ns,
					      const unsigned char **query,
					      int *querylen,
					      unsigned char *ans,
					      int anssiz,
					      int *resplen);

typedef res_sendhookact (*res_send_rhook)(const struct sockaddr_in *ns,
					      const unsigned char *query,
					      int querylen,
					      unsigned char *ans,
					      int anssiz,
					      int *resplen);

struct res_sym {
	int	number;		/* Identifying number, like T_MX */
	char *	name;		/* Its symbolic name, like "MX" */
	char *	humanname;	/* Its fun name, like "mail exchanger" */
};

extern struct __res_state _res;
#if 1 /* INET6 */
extern struct __res_state_ext _res_ext;
#endif
extern const struct res_sym __p_class_syms[];
extern const struct res_sym __p_type_syms[];

/* Private routines shared between libc/net, named, nslookup and others. */
#define	res_hnok	__res_hnok
#define	res_ownok	__res_ownok
#define	res_mailok	__res_mailok
#define	res_dnok	__res_dnok
#define	sym_ntos	__sym_ntos
#define b64_ntop	__b64_ntop
#define	b64_pton	__b64_pton
#define	dn_skipname	__dn_skipname
#define	putlong		__putlong
#define	putshort	__putshort
#define p_class		__p_class
#define p_type		__p_type
#define	dn_count_labels	__dn_count_labels
#define	dn_comp		__dn_comp
#define	res_randomid	__res_randomid
#define	res_send	__res_send
#define	res_opt		__res_opt

#ifdef BIND_RES_POSIX3
#define	dn_expand	__dn_expand
#define	res_init	__res_init
#define	res_query	__res_query
#define	res_search	__res_search
#define	res_querydomain	__res_querydomain
#define	res_mkquery	__res_mkquery
#endif

__BEGIN_DECLS
int			res_hnok(const char *);
int			res_ownok(const char *);
int			res_mailok(const char *);
int			res_dnok(const char *);
const char *		sym_ntos(const struct res_sym *, int, int *);
int			b64_ntop(unsigned char const *, size_t, char *, size_t);
int			b64_pton(char const *, unsigned char *, size_t);
int			dn_skipname(const unsigned char *, 
			    const unsigned char *);
void			putlong(u_int32_t, unsigned char *);
void			putshort(u_int16_t, unsigned char *);
const char *		p_class(int);
const char *		p_type(int);
int			dn_comp(const char *, unsigned char *, int,
			    unsigned char **, unsigned char **);
int			dn_expand(const unsigned char *, const unsigned char *, 
			    const unsigned char *, char *, int);
int			res_init(void);
unsigned int		res_randomid(void);
int			res_query(const char *, int, int, unsigned char *, int)
			__attribute__((__bounded__(__string__,4,5)));
int			res_search(const char *, int, int, unsigned char *, int)
			    __attribute__((__bounded__(__string__,4,5)));
int			res_querydomain(const char *, const char *, int, int,
			    unsigned char *, int)
			__attribute__((__bounded__(__string__,5,6)));
int			res_mkquery(int, const char *, int, int, 
			    const unsigned char *, int, const unsigned char *, 
			    unsigned char *, int)
			__attribute__((__bounded__(__string__,5,6)))
			__attribute__((__bounded__(__string__,8,9)));
int			res_send(const unsigned char *, int, unsigned char *, 
			    int)
			__attribute__((__bounded__(__string__,3,4)));
__END_DECLS

#endif /* !_RESOLV_H_ */
