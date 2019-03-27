#if !defined(lint) && !defined(SABER)
static const char rcsid[] = "$Id: res_findzonecut.c,v 1.10 2005/10/11 00:10:16 marka Exp $";
#endif /* not lint */

/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2004 by Internet Systems Consortium, Inc. ("ISC")
 * Copyright (c) 1999 by Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/* Import. */

#include "port_before.h"

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>

#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <isc/list.h>

#include "port_after.h"

#include <resolv.h>

/* Data structures. */

typedef struct rr_a {
	LINK(struct rr_a)	link;
	union res_sockaddr_union addr;
} rr_a;
typedef LIST(rr_a) rrset_a;

typedef struct rr_ns {
	LINK(struct rr_ns)	link;
	const char *		name;
	unsigned int		flags;
	rrset_a			addrs;
} rr_ns;
typedef LIST(rr_ns) rrset_ns;

#define	RR_NS_HAVE_V4		0x01
#define	RR_NS_HAVE_V6		0x02

/* Forward. */

static int	satisfy(res_state, const char *, rrset_ns *,
			union res_sockaddr_union *, int);
static int	add_addrs(res_state, rr_ns *,
			  union res_sockaddr_union *, int);
static int	get_soa(res_state, const char *, ns_class, int,
			char *, size_t, char *, size_t,
			rrset_ns *);
static int	get_ns(res_state, const char *, ns_class, int, rrset_ns *);
static int	get_glue(res_state, ns_class, int, rrset_ns *);
static int	save_ns(res_state, ns_msg *, ns_sect,
			const char *, ns_class, int, rrset_ns *);
static int	save_a(res_state, ns_msg *, ns_sect,
		       const char *, ns_class, int, rr_ns *);
static void	free_nsrrset(rrset_ns *);
static void	free_nsrr(rrset_ns *, rr_ns *);
static rr_ns *	find_ns(rrset_ns *, const char *);
static int	do_query(res_state, const char *, ns_class, ns_type,
			 u_char *, ns_msg *);
static void	res_dprintf(const char *, ...) ISC_FORMAT_PRINTF(1, 2);

/* Macros. */

#define DPRINTF(x) do {\
		int save_errno = errno; \
		if ((statp->options & RES_DEBUG) != 0U) res_dprintf x; \
		errno = save_errno; \
	} while (0)

/* Public. */

/*%
 *	find enclosing zone for a <dname,class>, and some server addresses
 *
 * parameters:
 *\li	res - resolver context to work within (is modified)
 *\li	dname - domain name whose enclosing zone is desired
 *\li	class - class of dname (and its enclosing zone)
 *\li	zname - found zone name
 *\li	zsize - allocated size of zname
 *\li	addrs - found server addresses
 *\li	naddrs - max number of addrs
 *
 * return values:
 *\li	< 0 - an error occurred (check errno)
 *\li	= 0 - zname is now valid, but addrs[] wasn't changed
 *\li	> 0 - zname is now valid, and return value is number of addrs[] found
 *
 * notes:
 *\li	this function calls res_nsend() which means it depends on correctly
 *	functioning recursive nameservers (usually defined in /etc/resolv.conf
 *	or its local equivalent).
 *
 *\li	we start by asking for an SOA<dname,class>.  if we get one as an
 *	answer, that just means <dname,class> is a zone top, which is fine.
 *	more than likely we'll be told to go pound sand, in the form of a
 *	negative answer.
 *
 *\li	note that we are not prepared to deal with referrals since that would
 *	only come from authority servers and our correctly functioning local
 *	recursive server would have followed the referral and got us something
 *	more definite.
 *
 *\li	if the authority section contains an SOA, this SOA should also be the
 *	closest enclosing zone, since any intermediary zone cuts would've been
 *	returned as referrals and dealt with by our correctly functioning local
 *	recursive name server.  but an SOA in the authority section should NOT
 *	match our dname (since that would have been returned in the answer
 *	section).  an authority section SOA has to be "above" our dname.
 *
 *\li	however, since authority section SOA's were once optional, it's
 *	possible that we'll have to go hunting for the enclosing SOA by
 *	ripping labels off the front of our dname -- this is known as "doing
 *	it the hard way."
 *
 *\li	ultimately we want some server addresses, which are ideally the ones
 *	pertaining to the SOA.MNAME, but only if there is a matching NS RR.
 *	so the second phase (after we find an SOA) is to go looking for the
 *	NS RRset for that SOA's zone.
 *
 *\li	no answer section processed by this code is allowed to contain CNAME
 *	or DNAME RR's.  for the SOA query this means we strip a label and
 *	keep going.  for the NS and A queries this means we just give up.
 */

#ifndef _LIBC
int
res_findzonecut(res_state statp, const char *dname, ns_class class, int opts,
		char *zname, size_t zsize, struct in_addr *addrs, int naddrs)
{
	int result, i;
	union res_sockaddr_union *u;

	
	opts |= RES_IPV4ONLY;
	opts &= ~RES_IPV6ONLY;

	u = calloc(naddrs, sizeof(*u));
	if (u == NULL)
		return(-1);

	result = res_findzonecut2(statp, dname, class, opts, zname, zsize,
				  u, naddrs);

	for (i = 0; i < result; i++) {
		addrs[i] = u[i].sin.sin_addr;
	}
	free(u);
	return (result);
}
#endif

int
res_findzonecut2(res_state statp, const char *dname, ns_class class, int opts,
		 char *zname, size_t zsize, union res_sockaddr_union *addrs,
		 int naddrs)
{
	char mname[NS_MAXDNAME];
	u_long save_pfcode;
	rrset_ns nsrrs;
	int n;

	DPRINTF(("START dname='%s' class=%s, zsize=%ld, naddrs=%d",
		 dname, p_class(class), (long)zsize, naddrs));
	save_pfcode = statp->pfcode;
	statp->pfcode |= RES_PRF_HEAD2 | RES_PRF_HEAD1 | RES_PRF_HEADX |
			 RES_PRF_QUES | RES_PRF_ANS |
			 RES_PRF_AUTH | RES_PRF_ADD;
	INIT_LIST(nsrrs);

	DPRINTF(("get the soa, and see if it has enough glue"));
	if ((n = get_soa(statp, dname, class, opts, zname, zsize,
			 mname, sizeof mname, &nsrrs)) < 0 ||
	    ((opts & RES_EXHAUSTIVE) == 0 &&
	     (n = satisfy(statp, mname, &nsrrs, addrs, naddrs)) > 0))
		goto done;

	DPRINTF(("get the ns rrset and see if it has enough glue"));
	if ((n = get_ns(statp, zname, class, opts, &nsrrs)) < 0 ||
	    ((opts & RES_EXHAUSTIVE) == 0 &&
	     (n = satisfy(statp, mname, &nsrrs, addrs, naddrs)) > 0))
		goto done;

	DPRINTF(("get the missing glue and see if it's finally enough"));
	if ((n = get_glue(statp, class, opts, &nsrrs)) >= 0)
		n = satisfy(statp, mname, &nsrrs, addrs, naddrs);

 done:
	DPRINTF(("FINISH n=%d (%s)", n, (n < 0) ? strerror(errno) : "OK"));
	free_nsrrset(&nsrrs);
	statp->pfcode = save_pfcode;
	return (n);
}

/* Private. */

static int
satisfy(res_state statp, const char *mname, rrset_ns *nsrrsp,
	union res_sockaddr_union *addrs, int naddrs)
{
	rr_ns *nsrr;
	int n, x;

	n = 0;
	nsrr = find_ns(nsrrsp, mname);
	if (nsrr != NULL) {
		x = add_addrs(statp, nsrr, addrs, naddrs);
		addrs += x;
		naddrs -= x;
		n += x;
	}
	for (nsrr = HEAD(*nsrrsp);
	     nsrr != NULL && naddrs > 0;
	     nsrr = NEXT(nsrr, link))
		if (ns_samename(nsrr->name, mname) != 1) {
			x = add_addrs(statp, nsrr, addrs, naddrs);
			addrs += x;
			naddrs -= x;
			n += x;
		}
	DPRINTF(("satisfy(%s): %d", mname, n));
	return (n);
}

static int
add_addrs(res_state statp, rr_ns *nsrr,
	  union res_sockaddr_union *addrs, int naddrs)
{
	rr_a *arr;
	int n = 0;

	for (arr = HEAD(nsrr->addrs); arr != NULL; arr = NEXT(arr, link)) {
		if (naddrs <= 0)
			return (0);
		*addrs++ = arr->addr;
		naddrs--;
		n++;
	}
	DPRINTF(("add_addrs: %d", n));
	return (n);
}

static int
get_soa(res_state statp, const char *dname, ns_class class, int opts,
	char *zname, size_t zsize, char *mname, size_t msize,
	rrset_ns *nsrrsp)
{
	char tname[NS_MAXDNAME];
	u_char *resp = NULL;
	int n, i, ancount, nscount;
	ns_sect sect;
	ns_msg msg;
	u_int rcode;

	/*
	 * Find closest enclosing SOA, even if it's for the root zone.
	 */

	/* First canonicalize dname (exactly one unescaped trailing "."). */
	if (ns_makecanon(dname, tname, sizeof tname) < 0)
		goto cleanup;
	dname = tname;

	resp = malloc(NS_MAXMSG);
	if (resp == NULL)
		goto cleanup;

	/* Now grovel the subdomains, hunting for an SOA answer or auth. */
	for (;;) {
		/* Leading or inter-label '.' are skipped here. */
		while (*dname == '.')
			dname++;

		/* Is there an SOA? */
		n = do_query(statp, dname, class, ns_t_soa, resp, &msg);
		if (n < 0) {
			DPRINTF(("get_soa: do_query('%s', %s) failed (%d)",
				 dname, p_class(class), n));
			goto cleanup;
		}
		if (n > 0) {
			DPRINTF(("get_soa: CNAME or DNAME found"));
			sect = ns_s_max, n = 0;
		} else {
			rcode = ns_msg_getflag(msg, ns_f_rcode);
			ancount = ns_msg_count(msg, ns_s_an);
			nscount = ns_msg_count(msg, ns_s_ns);
			if (ancount > 0 && rcode == ns_r_noerror)
				sect = ns_s_an, n = ancount;
			else if (nscount > 0)
				sect = ns_s_ns, n = nscount;
			else
				sect = ns_s_max, n = 0;
		}
		for (i = 0; i < n; i++) {
			const char *t;
			const u_char *rdata;
			ns_rr rr;

			if (ns_parserr(&msg, sect, i, &rr) < 0) {
				DPRINTF(("get_soa: ns_parserr(%s, %d) failed",
					 p_section(sect, ns_o_query), i));
				goto cleanup;
			}
			if (ns_rr_type(rr) == ns_t_cname ||
			    ns_rr_type(rr) == ns_t_dname)
				break;
			if (ns_rr_type(rr) != ns_t_soa ||
			    ns_rr_class(rr) != class)
				continue;
			t = ns_rr_name(rr);
			switch (sect) {
			case ns_s_an:
				if (ns_samedomain(dname, t) == 0) {
					DPRINTF(
				    ("get_soa: ns_samedomain('%s', '%s') == 0",
						dname, t)
						);
					errno = EPROTOTYPE;
					goto cleanup;
				}
				break;
			case ns_s_ns:
				if (ns_samename(dname, t) == 1 ||
				    ns_samedomain(dname, t) == 0) {
					DPRINTF(
		       ("get_soa: ns_samename() || !ns_samedomain('%s', '%s')",
						dname, t)
						);
					errno = EPROTOTYPE;
					goto cleanup;
				}
				break;
			default:
				abort();
			}
			if (strlen(t) + 1 > zsize) {
				DPRINTF(("get_soa: zname(%lu) too small (%lu)",
					 (unsigned long)zsize,
					 (unsigned long)strlen(t) + 1));
				errno = EMSGSIZE;
				goto cleanup;
			}
			strcpy(zname, t);
			rdata = ns_rr_rdata(rr);
			if (ns_name_uncompress(resp, ns_msg_end(msg), rdata,
					       mname, msize) < 0) {
				DPRINTF(("get_soa: ns_name_uncompress failed")
					);
				goto cleanup;
			}
			if (save_ns(statp, &msg, ns_s_ns,
				    zname, class, opts, nsrrsp) < 0) {
				DPRINTF(("get_soa: save_ns failed"));
				goto cleanup;
			}
			free(resp);
			return (0);
		}

		/* If we're out of labels, then not even "." has an SOA! */
		if (*dname == '\0')
			break;

		/* Find label-terminating "."; top of loop will skip it. */
		while (*dname != '.') {
			if (*dname == '\\')
				if (*++dname == '\0') {
					errno = EMSGSIZE;
					goto cleanup;
				}
			dname++;
		}
	}
	DPRINTF(("get_soa: out of labels"));
	errno = EDESTADDRREQ;
 cleanup:
	if (resp != NULL)
		free(resp);
	return (-1);
}

static int
get_ns(res_state statp, const char *zname, ns_class class, int opts,
      rrset_ns *nsrrsp)
{
	u_char *resp;
	ns_msg msg;
	int n;

	resp = malloc(NS_MAXMSG);
	if (resp == NULL)
		return (-1);

	/* Go and get the NS RRs for this zone. */
	n = do_query(statp, zname, class, ns_t_ns, resp, &msg);
	if (n != 0) {
		DPRINTF(("get_ns: do_query('%s', %s) failed (%d)",
			 zname, p_class(class), n));
		free(resp);
		return (-1);
	}

	/* Remember the NS RRs and associated A RRs that came back. */
	if (save_ns(statp, &msg, ns_s_an, zname, class, opts, nsrrsp) < 0) {
		DPRINTF(("get_ns save_ns('%s', %s) failed",
			 zname, p_class(class)));
		free(resp);
		return (-1);
	}

	free(resp);
	return (0);
}

static int
get_glue(res_state statp, ns_class class, int opts, rrset_ns *nsrrsp) {
	rr_ns *nsrr, *nsrr_n;
	u_char *resp;

	resp = malloc(NS_MAXMSG);
	if (resp == NULL)
		return(-1);

	/* Go and get the A RRs for each empty NS RR on our list. */
	for (nsrr = HEAD(*nsrrsp); nsrr != NULL; nsrr = nsrr_n) {
		ns_msg msg;
		int n;

		nsrr_n = NEXT(nsrr, link);

		if ((nsrr->flags & RR_NS_HAVE_V4) == 0) {
			n = do_query(statp, nsrr->name, class, ns_t_a,
				     resp, &msg);
			if (n < 0) {
				DPRINTF(
				       ("get_glue: do_query('%s', %s') failed",
					nsrr->name, p_class(class)));
				goto cleanup;
			}
			if (n > 0) {
				DPRINTF((
			"get_glue: do_query('%s', %s') CNAME or DNAME found",
					 nsrr->name, p_class(class)));
			}
			if (save_a(statp, &msg, ns_s_an, nsrr->name, class,
				   opts, nsrr) < 0) {
				DPRINTF(("get_glue: save_r('%s', %s) failed",
					 nsrr->name, p_class(class)));
				goto cleanup;
			}
		}

		if ((nsrr->flags & RR_NS_HAVE_V6) == 0) {
			n = do_query(statp, nsrr->name, class, ns_t_aaaa,
				     resp, &msg);
			if (n < 0) {
				DPRINTF(
				       ("get_glue: do_query('%s', %s') failed",
					nsrr->name, p_class(class)));
				goto cleanup;
			}
			if (n > 0) {
				DPRINTF((
			"get_glue: do_query('%s', %s') CNAME or DNAME found",
					 nsrr->name, p_class(class)));
			}
			if (save_a(statp, &msg, ns_s_an, nsrr->name, class,
				   opts, nsrr) < 0) {
				DPRINTF(("get_glue: save_r('%s', %s) failed",
					 nsrr->name, p_class(class)));
				goto cleanup;
			}
		}

		/* If it's still empty, it's just chaff. */
		if (EMPTY(nsrr->addrs)) {
			DPRINTF(("get_glue: removing empty '%s' NS",
				 nsrr->name));
			free_nsrr(nsrrsp, nsrr);
		}
	}
	free(resp);
	return (0);

 cleanup:
	free(resp);
	return (-1);
}

static int
save_ns(res_state statp, ns_msg *msg, ns_sect sect,
	const char *owner, ns_class class, int opts,
	rrset_ns *nsrrsp)
{
	int i;

	for (i = 0; i < ns_msg_count(*msg, sect); i++) {
		char tname[MAXDNAME];
		const u_char *rdata;
		rr_ns *nsrr;
		ns_rr rr;

		if (ns_parserr(msg, sect, i, &rr) < 0) {
			DPRINTF(("save_ns: ns_parserr(%s, %d) failed",
				 p_section(sect, ns_o_query), i));
			return (-1);
		}
		if (ns_rr_type(rr) != ns_t_ns ||
		    ns_rr_class(rr) != class ||
		    ns_samename(ns_rr_name(rr), owner) != 1)
			continue;
		nsrr = find_ns(nsrrsp, ns_rr_name(rr));
		if (nsrr == NULL) {
			nsrr = malloc(sizeof *nsrr);
			if (nsrr == NULL) {
				DPRINTF(("save_ns: malloc failed"));
				return (-1);
			}
			rdata = ns_rr_rdata(rr);
			if (ns_name_uncompress(ns_msg_base(*msg),
					       ns_msg_end(*msg), rdata,
					       tname, sizeof tname) < 0) {
				DPRINTF(("save_ns: ns_name_uncompress failed")
					);
				free(nsrr);
				return (-1);
			}
			nsrr->name = strdup(tname);
			if (nsrr->name == NULL) {
				DPRINTF(("save_ns: strdup failed"));
				free(nsrr);
				return (-1);
			}
			INIT_LINK(nsrr, link);
			INIT_LIST(nsrr->addrs);
			nsrr->flags = 0;
			APPEND(*nsrrsp, nsrr, link);
		}
		if (save_a(statp, msg, ns_s_ar,
			   nsrr->name, class, opts, nsrr) < 0) {
			DPRINTF(("save_ns: save_r('%s', %s) failed",
				 nsrr->name, p_class(class)));
			return (-1);
		}
	}
	return (0);
}

static int
save_a(res_state statp, ns_msg *msg, ns_sect sect,
       const char *owner, ns_class class, int opts,
       rr_ns *nsrr)
{
	int i;

	for (i = 0; i < ns_msg_count(*msg, sect); i++) {
		ns_rr rr;
		rr_a *arr;

		if (ns_parserr(msg, sect, i, &rr) < 0) {
			DPRINTF(("save_a: ns_parserr(%s, %d) failed",
				 p_section(sect, ns_o_query), i));
			return (-1);
		}
		if ((ns_rr_type(rr) != ns_t_a &&
		     ns_rr_type(rr) != ns_t_aaaa) ||
		    ns_rr_class(rr) != class ||
		    ns_samename(ns_rr_name(rr), owner) != 1 ||
		    ns_rr_rdlen(rr) != NS_INADDRSZ)
			continue;
		if ((opts & RES_IPV6ONLY) != 0 && ns_rr_type(rr) != ns_t_aaaa)
			continue;
		if ((opts & RES_IPV4ONLY) != 0 && ns_rr_type(rr) != ns_t_a)
			continue;
		arr = malloc(sizeof *arr);
		if (arr == NULL) {
			DPRINTF(("save_a: malloc failed"));
			return (-1);
		}
		INIT_LINK(arr, link);
		memset(&arr->addr, 0, sizeof(arr->addr));
		switch (ns_rr_type(rr)) {
		case ns_t_a:
			arr->addr.sin.sin_family = AF_INET;
#ifdef HAVE_SA_LEN
			arr->addr.sin.sin_len = sizeof(arr->addr.sin);
#endif
			memcpy(&arr->addr.sin.sin_addr, ns_rr_rdata(rr),
			       NS_INADDRSZ);
			arr->addr.sin.sin_port = htons(NAMESERVER_PORT);
			nsrr->flags |= RR_NS_HAVE_V4;
			break;
		case ns_t_aaaa:
			arr->addr.sin6.sin6_family = AF_INET6;
#ifdef HAVE_SA_LEN
			arr->addr.sin6.sin6_len = sizeof(arr->addr.sin6);
#endif
			memcpy(&arr->addr.sin6.sin6_addr, ns_rr_rdata(rr), 16);
			arr->addr.sin.sin_port = htons(NAMESERVER_PORT);
			nsrr->flags |= RR_NS_HAVE_V6;
			break;
		default:
			abort();
		}
		APPEND(nsrr->addrs, arr, link);
	}
	return (0);
}

static void
free_nsrrset(rrset_ns *nsrrsp) {
	rr_ns *nsrr;

	while ((nsrr = HEAD(*nsrrsp)) != NULL)
		free_nsrr(nsrrsp, nsrr);
}

static void
free_nsrr(rrset_ns *nsrrsp, rr_ns *nsrr) {
	rr_a *arr;
	char *tmp;

	while ((arr = HEAD(nsrr->addrs)) != NULL) {
		UNLINK(nsrr->addrs, arr, link);
		free(arr);
	}
	DE_CONST(nsrr->name, tmp);
	free(tmp);
	UNLINK(*nsrrsp, nsrr, link);
	free(nsrr);
}

static rr_ns *
find_ns(rrset_ns *nsrrsp, const char *dname) {
	rr_ns *nsrr;

	for (nsrr = HEAD(*nsrrsp); nsrr != NULL; nsrr = NEXT(nsrr, link))
		if (ns_samename(nsrr->name, dname) == 1)
			return (nsrr);
	return (NULL);
}

static int
do_query(res_state statp, const char *dname, ns_class class, ns_type qtype,
	 u_char *resp, ns_msg *msg)
{
	u_char req[NS_PACKETSZ];
	int i, n;

	n = res_nmkquery(statp, ns_o_query, dname, class, qtype,
			 NULL, 0, NULL, req, NS_PACKETSZ);
	if (n < 0) {
		DPRINTF(("do_query: res_nmkquery failed"));
		return (-1);
	}
	n = res_nsend(statp, req, n, resp, NS_MAXMSG);
	if (n < 0) {
		DPRINTF(("do_query: res_nsend failed"));
		return (-1);
	}
	if (n == 0) {
		DPRINTF(("do_query: res_nsend returned 0"));
		errno = EMSGSIZE;
		return (-1);
	}
	if (ns_initparse(resp, n, msg) < 0) {
		DPRINTF(("do_query: ns_initparse failed"));
		return (-1);
	}
	n = 0;
	for (i = 0; i < ns_msg_count(*msg, ns_s_an); i++) {
		ns_rr rr;

		if (ns_parserr(msg, ns_s_an, i, &rr) < 0) {
			DPRINTF(("do_query: ns_parserr failed"));
			return (-1);
		}
		n += (ns_rr_class(rr) == class &&
		      (ns_rr_type(rr) == ns_t_cname ||
		       ns_rr_type(rr) == ns_t_dname));
	}
	return (n);
}

static void
res_dprintf(const char *fmt, ...) {
	va_list ap;

	va_start(ap, fmt);
	fputs(";; res_findzonecut: ", stderr);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

/*! \file */
