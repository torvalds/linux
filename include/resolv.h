/*-
 * SPDX-License-Identifier: (ISC AND BSD-3-Clause)
 *
 * Portions Copyright (C) 2004, 2005, 2008, 2009  Internet Systems Consortium, Inc. ("ISC")
 * Portions Copyright (C) 1995-2003  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Copyright (c) 1983, 1987, 1989
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
 */

/*%
 *	@(#)resolv.h	8.1 (Berkeley) 6/2/93
 *	$Id: resolv.h,v 1.30 2009/03/03 01:52:48 each Exp $
 * $FreeBSD$
 */

#ifndef _RESOLV_H_
#define	_RESOLV_H_

#include <sys/param.h>
#include <sys/types.h>
#include <sys/cdefs.h>
#include <sys/socket.h>
#include <stdio.h>
#include <arpa/nameser.h>

/*%
 * Revision information.  This is the release date in YYYYMMDD format.
 * It can change every day so the right thing to do with it is use it
 * in preprocessor commands such as "#if (__RES > 19931104)".  Do not
 * compare for equality; rather, use it to determine whether your resolver
 * is new enough to contain a certain feature.
 */

#define	__RES	20090302

/*%
 * This used to be defined in res_query.c, now it's in herror.c.
 * [XXX no it's not.  It's in irs/irs_data.c]
 * It was
 * never extern'd by any *.h file before it was placed here.  For thread
 * aware programs, the last h_errno value set is stored in res->h_errno.
 *
 * XXX:	There doesn't seem to be a good reason for exposing RES_SET_H_ERRNO
 *	(and __h_errno_set) to the public via <resolv.h>.
 * XXX:	__h_errno_set is really part of IRS, not part of the resolver.
 *	If somebody wants to build and use a resolver that doesn't use IRS,
 *	what do they do?  Perhaps something like
 *		#ifdef WANT_IRS
 *		# define RES_SET_H_ERRNO(r,x) __h_errno_set(r,x)
 *		#else
 *		# define RES_SET_H_ERRNO(r,x) (h_errno = (r)->res_h_errno = (x))
 *		#endif
 */

#define RES_SET_H_ERRNO(r,x) __h_errno_set(r,x)
struct __res_state; /*%< forward */
__BEGIN_DECLS
void __h_errno_set(struct __res_state *, int);
__END_DECLS

/*%
 * Resolver configuration file.
 * Normally not present, but may contain the address of the
 * initial name server(s) to query and the domain search list.
 */

#ifndef _PATH_RESCONF
#define	_PATH_RESCONF        "/etc/resolv.conf"
#endif

typedef enum { res_goahead, res_nextns, res_modified, res_done, res_error }
	res_sendhookact;

typedef res_sendhookact (*res_send_qhook)(struct sockaddr * const *,
					  const u_char **, int *,
					  u_char *, int, int *);

typedef res_sendhookact (*res_send_rhook)(const struct sockaddr *,
					  const u_char *, int, u_char *,
					  int, int *);

struct res_sym {
	int		number;	   /*%< Identifying number, like T_MX */
	const char *	name;	   /*%< Its symbolic name, like "MX" */
	const char *	humanname; /*%< Its fun name, like "mail exchanger" */
};

/*%
 * Global defines and variables for resolver stub.
 */
#define	MAXNS			3	/*%< max # name servers we'll track */
#define	MAXDFLSRCH		3	/*%< # default domain levels to try */
#define	MAXDNSRCH		6	/*%< max # domains in search path */
#define	LOCALDOMAINPARTS	2	/*%< min levels in name that is "local" */
#define	RES_TIMEOUT		5	/*%< min. seconds between retries */
#define	MAXRESOLVSORT		10	/*%< number of net to sort on */
#define	RES_MAXNDOTS		15	/*%< should reflect bit field size */
#define	RES_MAXRETRANS		30	/*%< only for resolv.conf/RES_OPTIONS */
#define	RES_MAXRETRY		5	/*%< only for resolv.conf/RES_OPTIONS */
#define	RES_DFLRETRY		2	/*%< Default #/tries. */
#define	RES_MAXTIME		65535	/*%< Infinity, in milliseconds. */
struct __res_state_ext;

struct __res_state {
	int	retrans;	 	/*%< retransmission time interval */
	int	retry;			/*%< number of times to retransmit */
	/*
	 * XXX: If `sun' is defined, `options' and `pfcode' are
	 * defined as u_int in original BIND9 distribution.  However,
	 * it breaks binary backward compatibility against FreeBSD's
	 * resolver.  So, we changed not to see `sun'.
	 */
#if defined(sun) && 0
	u_int	options;		/*%< option flags - see below. */
#else
	u_long	options;		/*%< option flags - see below. */
#endif
	int	nscount;		/*%< number of name servers */
	struct sockaddr_in
		nsaddr_list[MAXNS];	/*%< address of name server */
#define	nsaddr	nsaddr_list[0]		/*%< for backward compatibility */
	u_short	id;			/*%< current message id */
	char	*dnsrch[MAXDNSRCH+1];	/*%< components of domain to search */
	char	defdname[256];		/*%< default domain (deprecated) */
#if defined(sun) && 0
	u_int	pfcode;			/*%< RES_PRF_ flags - see below. */
#else
	u_long	pfcode;			/*%< RES_PRF_ flags - see below. */
#endif
	unsigned ndots:4;		/*%< threshold for initial abs. query */
	unsigned nsort:4;		/*%< number of elements in sort_list[] */
	char	unused[3];
	struct {
		struct in_addr	addr;
		u_int32_t	mask;
	} sort_list[MAXRESOLVSORT];
	res_send_qhook qhook;		/*%< query hook */
	res_send_rhook rhook;		/*%< response hook */
	int	res_h_errno;		/*%< last one set for this context */
	int	_vcsock;		/*%< PRIVATE: for res_send VC i/o */
	u_int	_flags;			/*%< PRIVATE: see below */
	u_int	_pad;			/*%< make _u 64 bit aligned */
	union {
		/* On an 32-bit arch this means 512b total. */
		char	pad[72 - 4*sizeof (int) - 3*sizeof (void *)];
		struct {
			u_int16_t		nscount;
			u_int16_t		nstimes[MAXNS];	/*%< ms. */
			int			nssocks[MAXNS];
			struct __res_state_ext *ext;	/*%< extension for IPv6 */
		} _ext;
	} _u;
	u_char	*_rnd;			/*%< PRIVATE: random state */
};

typedef struct __res_state *res_state;

union res_sockaddr_union {
	struct sockaddr_in	sin;
#ifdef IN6ADDR_ANY_INIT
	struct sockaddr_in6	sin6;
#endif
#ifdef ISC_ALIGN64
	int64_t			__align64;	/*%< 64bit alignment */
#else
	int32_t			__align32;	/*%< 32bit alignment */
#endif
	char			__space[128];   /*%< max size */
};

/*%
 * Resolver flags (used to be discrete per-module statics ints).
 */
#define	RES_F_VC	0x00000001	/*%< socket is TCP */
#define	RES_F_CONN	0x00000002	/*%< socket is connected */
#define	RES_F_EDNS0ERR	0x00000004	/*%< EDNS0 caused errors */
#define	RES_F__UNUSED	0x00000008	/*%< (unused) */
#define	RES_F_LASTMASK	0x000000F0	/*%< ordinal server of last res_nsend */
#define	RES_F_LASTSHIFT	4		/*%< bit position of LASTMASK "flag" */
#define	RES_GETLAST(res) (((res)._flags & RES_F_LASTMASK) >> RES_F_LASTSHIFT)

/* res_findzonecut2() options */
#define	RES_EXHAUSTIVE	0x00000001	/*%< always do all queries */
#define	RES_IPV4ONLY	0x00000002	/*%< IPv4 only */
#define	RES_IPV6ONLY	0x00000004	/*%< IPv6 only */

/*%
 * Resolver options (keep these in synch with res_debug.c, please)
 */
#define RES_INIT	0x00000001	/*%< address initialized */
#define RES_DEBUG	0x00000002	/*%< print debug messages */
#define RES_AAONLY	0x00000004	/*%< authoritative answers only (!IMPL)*/
#define RES_USEVC	0x00000008	/*%< use virtual circuit */
#define RES_PRIMARY	0x00000010	/*%< query primary server only (!IMPL) */
#define RES_IGNTC	0x00000020	/*%< ignore truncation errors */
#define RES_RECURSE	0x00000040	/*%< recursion desired */
#define RES_DEFNAMES	0x00000080	/*%< use default domain name */
#define RES_STAYOPEN	0x00000100	/*%< Keep TCP socket open */
#define RES_DNSRCH	0x00000200	/*%< search up local domain tree */
#define	RES_INSECURE1	0x00000400	/*%< type 1 security disabled */
#define	RES_INSECURE2	0x00000800	/*%< type 2 security disabled */
#define	RES_NOALIASES	0x00001000	/*%< shuts off HOSTALIASES feature */
#define	RES_USE_INET6	0x00002000	/*%< use/map IPv6 in gethostbyname() */
#define RES_ROTATE	0x00004000	/*%< rotate ns list after each query */
#define	RES_NOCHECKNAME	0x00008000	/*%< do not check names for sanity. */
#define	RES_KEEPTSIG	0x00010000	/*%< do not strip TSIG records */
#define	RES_BLAST	0x00020000	/*%< blast all recursive servers */
#define RES_NSID	0x00040000      /*%< request name server ID */
#define RES_NOTLDQUERY	0x00100000	/*%< don't unqualified name as a tld */
#define RES_USE_DNSSEC	0x00200000	/*%< use DNSSEC using OK bit in OPT */
/* #define RES_DEBUG2	0x00400000 */	/* nslookup internal */
/* KAME extensions: use higher bit to avoid conflict with ISC use */
#define RES_USE_DNAME	0x10000000	/*%< use DNAME */
#define RES_USE_EDNS0	0x40000000	/*%< use EDNS0 if configured */
#define RES_NO_NIBBLE2	0x80000000	/*%< disable alternate nibble lookup */

#define RES_DEFAULT	(RES_RECURSE | RES_DEFNAMES | \
			 RES_DNSRCH | RES_NO_NIBBLE2)

/*%
 * Resolver "pfcode" values.  Used by dig.
 */
#define	RES_PRF_STATS	0x00000001
#define	RES_PRF_UPDATE	0x00000002
#define	RES_PRF_CLASS   0x00000004
#define	RES_PRF_CMD	0x00000008
#define	RES_PRF_QUES	0x00000010
#define	RES_PRF_ANS	0x00000020
#define	RES_PRF_AUTH	0x00000040
#define	RES_PRF_ADD	0x00000080
#define	RES_PRF_HEAD1	0x00000100
#define	RES_PRF_HEAD2	0x00000200
#define	RES_PRF_TTLID	0x00000400
#define	RES_PRF_HEADX	0x00000800
#define	RES_PRF_QUERY	0x00001000
#define	RES_PRF_REPLY	0x00002000
#define	RES_PRF_INIT	0x00004000
#define	RES_PRF_TRUNC	0x00008000
/*			0x00010000	*/

/* Things involving an internal (static) resolver context. */
__BEGIN_DECLS
extern struct __res_state *__res_state(void);
__END_DECLS
#define _res (*__res_state())

#ifndef __BIND_NOSTATIC
#define fp_nquery		__fp_nquery
#define fp_query		__fp_query
#define hostalias		__hostalias
#define p_query			__p_query
#define res_close		__res_close
#define res_init		__res_init
#define res_isourserver		__res_isourserver
#define res_mkquery		__res_mkquery
#define res_opt			__res_opt
#define res_query		__res_query
#define res_querydomain		__res_querydomain
#define res_search		__res_search
#define res_send		__res_send
#define res_sendsigned		__res_sendsigned

__BEGIN_DECLS
void		fp_nquery(const u_char *, int, FILE *);
void		fp_query(const u_char *, FILE *);
const char *	hostalias(const char *);
void		p_query(const u_char *);
void		res_close(void);
int		res_init(void);
int		res_isourserver(const struct sockaddr_in *);
int		res_mkquery(int, const char *, int, int, const u_char *,
				 int, const u_char *, u_char *, int);
int		res_opt(int, u_char *, int, int);
int		res_query(const char *, int, int, u_char *, int);
int		res_querydomain(const char *, const char *, int, int,
				     u_char *, int);
int		res_search(const char *, int, int, u_char *, int);
int		res_send(const u_char *, int, u_char *, int);
int		res_sendsigned(const u_char *, int, ns_tsig_key *,
				    u_char *, int);
__END_DECLS
#endif

#if !defined(SHARED_LIBBIND) || defined(LIB)
/*
 * If libbind is a shared object (well, DLL anyway)
 * these externs break the linker when resolv.h is
 * included by a lib client (like named)
 * Make them go away if a client is including this
 *
 */
extern const struct res_sym __p_key_syms[];
extern const struct res_sym __p_cert_syms[];
extern const struct res_sym __p_class_syms[];
extern const struct res_sym __p_type_syms[];
extern const struct res_sym __p_rcode_syms[];
#endif /* SHARED_LIBBIND */

#define b64_ntop		__b64_ntop
#define b64_pton		__b64_pton
#define dn_comp			__dn_comp
#define dn_count_labels		__dn_count_labels
#define dn_expand		__dn_expand
#define dn_skipname		__dn_skipname
#define fp_resstat		__fp_resstat
#define loc_aton		__loc_aton
#define loc_ntoa		__loc_ntoa
#define p_cdname		__p_cdname
#define p_cdnname		__p_cdnname
#define p_class			__p_class
#define p_fqname		__p_fqname
#define p_fqnname		__p_fqnname
#define p_option		__p_option
#define p_secstodate		__p_secstodate
#define p_section		__p_section
#define p_time			__p_time
#define p_type			__p_type
#define p_rcode			__p_rcode
#define p_sockun		__p_sockun
#define putlong			__putlong
#define putshort		__putshort
#define res_dnok		__res_dnok
#if 0
#define res_findzonecut		__res_findzonecut
#endif
#define res_findzonecut2	__res_findzonecut2
#define res_hnok		__res_hnok
#define res_hostalias		__res_hostalias
#define res_mailok		__res_mailok
#define res_nameinquery		__res_nameinquery
#define res_nclose		__res_nclose
#define res_ninit		__res_ninit
#define res_nmkquery		__res_nmkquery
#define res_pquery		__res_pquery
#define res_nquery		__res_nquery
#define res_nquerydomain	__res_nquerydomain
#define res_nsearch		__res_nsearch
#define res_nsend		__res_nsend
#if 0
#define res_nsendsigned		__res_nsendsigned
#endif
#define res_nisourserver	__res_nisourserver
#define res_ownok		__res_ownok
#define res_queriesmatch	__res_queriesmatch
#define res_rndinit		__res_rndinit
#define res_randomid		__res_randomid
#define res_nrandomid		__res_nrandomid
#define sym_ntop		__sym_ntop
#define sym_ntos		__sym_ntos
#define sym_ston		__sym_ston
#define res_nopt		__res_nopt
#define res_nopt_rdata       	__res_nopt_rdata
#define res_ndestroy		__res_ndestroy
#define	res_nametoclass		__res_nametoclass
#define	res_nametotype		__res_nametotype
#define	res_setservers		__res_setservers
#define	res_getservers		__res_getservers
#if 0
#define	res_buildprotolist	__res_buildprotolist
#define	res_destroyprotolist	__res_destroyprotolist
#define	res_destroyservicelist	__res_destroyservicelist
#define	res_get_nibblesuffix	__res_get_nibblesuffix
#define	res_get_nibblesuffix2	__res_get_nibblesuffix2
#endif
#define	res_ourserver_p		__res_ourserver_p
#if 0
#define	res_protocolname	__res_protocolname
#define	res_protocolnumber	__res_protocolnumber
#endif
#define	res_send_setqhook	__res_send_setqhook
#define	res_send_setrhook	__res_send_setrhook
#if 0
#define	res_servicename		__res_servicename
#define	res_servicenumber	__res_servicenumber
#endif
__BEGIN_DECLS
int		res_hnok(const char *);
int		res_ownok(const char *);
int		res_mailok(const char *);
int		res_dnok(const char *);
int		sym_ston(const struct res_sym *, const char *, int *);
const char *	sym_ntos(const struct res_sym *, int, int *);
const char *	sym_ntop(const struct res_sym *, int, int *);
int		b64_ntop(u_char const *, size_t, char *, size_t);
int		b64_pton(char const *, u_char *, size_t);
int		loc_aton(const char *, u_char *);
const char *	loc_ntoa(const u_char *, char *);
int		dn_skipname(const u_char *, const u_char *);
void		putlong(u_int32_t, u_char *);
void		putshort(u_int16_t, u_char *);
#ifndef __ultrix__
u_int16_t	_getshort(const u_char *);
u_int32_t	_getlong(const u_char *);
#endif
const char *	p_class(int);
const char *	p_time(u_int32_t);
const char *	p_type(int);
const char *	p_rcode(int);
const char *	p_sockun(union res_sockaddr_union, char *, size_t);
const u_char *	p_cdnname(const u_char *, const u_char *, int, FILE *);
const u_char *	p_cdname(const u_char *, const u_char *, FILE *);
const u_char *	p_fqnname(const u_char *, const u_char *, int, char *, int);
const u_char *	p_fqname(const u_char *, const u_char *, FILE *);
const char *	p_option(u_long);
char *		p_secstodate(u_long);
int		dn_count_labels(const char *);
int		dn_comp(const char *, u_char *, int, u_char **, u_char **);
int		dn_expand(const u_char *, const u_char *, const u_char *,
			  char *, int);
void		res_rndinit(res_state);
u_int		res_randomid(void);
u_int		res_nrandomid(res_state);
int		res_nameinquery(const char *, int, int, const u_char *,
				const u_char *);
int		res_queriesmatch(const u_char *, const u_char *,
				 const u_char *, const u_char *);
const char *	p_section(int, int);
/* Things involving a resolver context. */
int		res_ninit(res_state);
int		res_nisourserver(const res_state, const struct sockaddr_in *);
void		fp_resstat(const res_state, FILE *);
void		res_pquery(const res_state, const u_char *, int, FILE *);
const char *	res_hostalias(const res_state, const char *, char *, size_t);
int		res_nquery(res_state, const char *, int, int, u_char *, int);
int		res_nsearch(res_state, const char *, int, int, u_char *, int);
int		res_nquerydomain(res_state, const char *, const char *,
				 int, int, u_char *, int);
int		res_nmkquery(res_state, int, const char *, int, int,
			     const u_char *, int, const u_char *,
			     u_char *, int);
int		res_nsend(res_state, const u_char *, int, u_char *, int);
#if 0
int		res_nsendsigned(res_state, const u_char *, int,
				ns_tsig_key *, u_char *, int);
int		res_findzonecut(res_state, const char *, ns_class, int,
				char *, size_t, struct in_addr *, int);
#endif
int		res_findzonecut2(res_state, const char *, ns_class, int,
				 char *, size_t,
				 union res_sockaddr_union *, int);
void		res_nclose(res_state);
int		res_nopt(res_state, int, u_char *, int, int);
int		res_nopt_rdata(res_state, int, u_char *, int, u_char *,
			       u_short, u_short, u_char *);
void		res_send_setqhook(res_send_qhook);
void		res_send_setrhook(res_send_rhook);
int		__res_vinit(res_state, int);
#if 0
void		res_destroyservicelist(void);
const char *	res_servicename(u_int16_t, const char *);
const char *	res_protocolname(int);
void		res_destroyprotolist(void);
void		res_buildprotolist(void);
const char *	res_get_nibblesuffix(res_state);
const char *	res_get_nibblesuffix2(res_state);
#endif
void		res_ndestroy(res_state);
u_int16_t	res_nametoclass(const char *, int *);
u_int16_t	res_nametotype(const char *, int *);
void		res_setservers(res_state, const union res_sockaddr_union *,
			       int);
int		res_getservers(res_state, union res_sockaddr_union *, int);
__END_DECLS

#endif /* !_RESOLV_H_ */
/*! \file */
