/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * ++Copyright++ 1985, 1988, 1993
 * -
 * Copyright (c) 1985, 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
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

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)gethostnamadr.c	8.1 (Berkeley) 6/4/93";
static char fromrcsid[] = "From: Id: gethnamaddr.c,v 8.23 1998/04/07 04:59:46 vixie Exp $";
#endif /* LIBC_SCCS and not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <resolv.h>
#include <ctype.h>
#include <errno.h>
#include <syslog.h>
#include <stdarg.h>
#include <nsswitch.h>

#include "netdb_private.h"
#include "res_config.h"

#define SPRINTF(x) ((size_t)sprintf x)

static const char AskedForGot[] =
		"gethostby*.gethostanswer: asked for \"%s\", got \"%s\"";

#ifdef RESOLVSORT
static void addrsort(char **, int, res_state);
#endif

#ifdef DEBUG
static void dbg_printf(char *, int, res_state) __printflike(1, 0);
#endif

#define MAXPACKET	(64*1024)

typedef union {
    HEADER hdr;
    u_char buf[MAXPACKET];
} querybuf;

typedef union {
    int32_t al;
    char ac;
} align;

int _dns_ttl_;

#ifdef DEBUG
static void
dbg_printf(char *msg, int num, res_state res)
{
	if (res->options & RES_DEBUG) {
		int save = errno;

		printf(msg, num);
		errno = save;
	}
}
#else
# define dbg_printf(msg, num, res) /*nada*/
#endif

#define BOUNDED_INCR(x) \
	do { \
		cp += x; \
		if (cp > eom) { \
			RES_SET_H_ERRNO(statp, NO_RECOVERY); \
			return (-1); \
		} \
	} while (0)

#define BOUNDS_CHECK(ptr, count) \
	do { \
		if ((ptr) + (count) > eom) { \
			RES_SET_H_ERRNO(statp, NO_RECOVERY); \
			return (-1); \
		} \
	} while (0)

static int
gethostanswer(const querybuf *answer, int anslen, const char *qname, int qtype,
    struct hostent *he, struct hostent_data *hed, res_state statp)
{
	const HEADER *hp;
	const u_char *cp;
	int n;
	const u_char *eom, *erdata;
	char *bp, *ep, **ap, **hap;
	int type, class, ancount, qdcount;
	int haveanswer, had_error;
	int toobig = 0;
	char tbuf[MAXDNAME];
	const char *tname;
	int (*name_ok)(const char *);

	tname = qname;
	he->h_name = NULL;
	eom = answer->buf + anslen;
	switch (qtype) {
	case T_A:
	case T_AAAA:
		name_ok = res_hnok;
		break;
	case T_PTR:
		name_ok = res_dnok;
		break;
	default:
		RES_SET_H_ERRNO(statp, NO_RECOVERY);
		return (-1);	/* XXX should be abort(); */
	}
	/*
	 * find first satisfactory answer
	 */
	hp = &answer->hdr;
	ancount = ntohs(hp->ancount);
	qdcount = ntohs(hp->qdcount);
	bp = hed->hostbuf;
	ep = hed->hostbuf + sizeof hed->hostbuf;
	cp = answer->buf;
	BOUNDED_INCR(HFIXEDSZ);
	if (qdcount != 1) {
		RES_SET_H_ERRNO(statp, NO_RECOVERY);
		return (-1);
	}
	n = dn_expand(answer->buf, eom, cp, bp, ep - bp);
	if ((n < 0) || !(*name_ok)(bp)) {
		RES_SET_H_ERRNO(statp, NO_RECOVERY);
		return (-1);
	}
	BOUNDED_INCR(n + QFIXEDSZ);
	if (qtype == T_A || qtype == T_AAAA) {
		/* res_send() has already verified that the query name is the
		 * same as the one we sent; this just gets the expanded name
		 * (i.e., with the succeeding search-domain tacked on).
		 */
		n = strlen(bp) + 1;		/* for the \0 */
		if (n >= MAXHOSTNAMELEN) {
			RES_SET_H_ERRNO(statp, NO_RECOVERY);
			return (-1);
		}
		he->h_name = bp;
		bp += n;
		/* The qname can be abbreviated, but h_name is now absolute. */
		qname = he->h_name;
	}
	ap = hed->host_aliases;
	*ap = NULL;
	he->h_aliases = hed->host_aliases;
	hap = hed->h_addr_ptrs;
	*hap = NULL;
	he->h_addr_list = hed->h_addr_ptrs;
	haveanswer = 0;
	had_error = 0;
	_dns_ttl_ = -1;
	while (ancount-- > 0 && cp < eom && !had_error) {
		n = dn_expand(answer->buf, eom, cp, bp, ep - bp);
		if ((n < 0) || !(*name_ok)(bp)) {
			had_error++;
			continue;
		}
		cp += n;			/* name */
		BOUNDS_CHECK(cp, 3 * INT16SZ + INT32SZ);
		type = _getshort(cp);
 		cp += INT16SZ;			/* type */
		class = _getshort(cp);
 		cp += INT16SZ;			/* class */
		if (qtype == T_A  && type == T_A)
			_dns_ttl_ = _getlong(cp);
		cp += INT32SZ;			/* TTL */
		n = _getshort(cp);
		cp += INT16SZ;			/* len */
		BOUNDS_CHECK(cp, n);
		erdata = cp + n;
		if (class != C_IN) {
			/* XXX - debug? syslog? */
			cp += n;
			continue;		/* XXX - had_error++ ? */
		}
		if ((qtype == T_A || qtype == T_AAAA) && type == T_CNAME) {
			if (ap >= &hed->host_aliases[_MAXALIASES-1])
				continue;
			n = dn_expand(answer->buf, eom, cp, tbuf, sizeof tbuf);
			if ((n < 0) || !(*name_ok)(tbuf)) {
				had_error++;
				continue;
			}
			cp += n;
			if (cp != erdata) {
				RES_SET_H_ERRNO(statp, NO_RECOVERY);
				return (-1);
			}
			/* Store alias. */
			*ap++ = bp;
			n = strlen(bp) + 1;	/* for the \0 */
			if (n >= MAXHOSTNAMELEN) {
				had_error++;
				continue;
			}
			bp += n;
			/* Get canonical name. */
			n = strlen(tbuf) + 1;	/* for the \0 */
			if (n > ep - bp || n >= MAXHOSTNAMELEN) {
				had_error++;
				continue;
			}
			strcpy(bp, tbuf);
			he->h_name = bp;
			bp += n;
			continue;
		}
		if (qtype == T_PTR && type == T_CNAME) {
			n = dn_expand(answer->buf, eom, cp, tbuf, sizeof tbuf);
			if (n < 0 || !res_dnok(tbuf)) {
				had_error++;
				continue;
			}
			cp += n;
			if (cp != erdata) {
				RES_SET_H_ERRNO(statp, NO_RECOVERY);
				return (-1);
			}
			/* Get canonical name. */
			n = strlen(tbuf) + 1;	/* for the \0 */
			if (n > ep - bp || n >= MAXHOSTNAMELEN) {
				had_error++;
				continue;
			}
			strcpy(bp, tbuf);
			tname = bp;
			bp += n;
			continue;
		}
		if (type != qtype) {
			if (type != T_SIG && type != ns_t_dname)
				syslog(LOG_NOTICE|LOG_AUTH,
	"gethostby*.gethostanswer: asked for \"%s %s %s\", got type \"%s\"",
				       qname, p_class(C_IN), p_type(qtype),
				       p_type(type));
			cp += n;
			continue;		/* XXX - had_error++ ? */
		}
		switch (type) {
		case T_PTR:
			if (strcasecmp(tname, bp) != 0) {
				syslog(LOG_NOTICE|LOG_AUTH,
				       AskedForGot, qname, bp);
				cp += n;
				continue;	/* XXX - had_error++ ? */
			}
			n = dn_expand(answer->buf, eom, cp, bp, ep - bp);
			if ((n < 0) || !res_hnok(bp)) {
				had_error++;
				break;
			}
#if MULTI_PTRS_ARE_ALIASES
			cp += n;
			if (cp != erdata) {
				RES_SET_H_ERRNO(statp, NO_RECOVERY);
				return (-1);
			}
			if (!haveanswer)
				he->h_name = bp;
			else if (ap < &hed->host_aliases[_MAXALIASES-1])
				*ap++ = bp;
			else
				n = -1;
			if (n != -1) {
				n = strlen(bp) + 1;	/* for the \0 */
				if (n >= MAXHOSTNAMELEN) {
					had_error++;
					break;
				}
				bp += n;
			}
			break;
#else
			he->h_name = bp;
			if (statp->options & RES_USE_INET6) {
				n = strlen(bp) + 1;	/* for the \0 */
				if (n >= MAXHOSTNAMELEN) {
					had_error++;
					break;
				}
				bp += n;
				_map_v4v6_hostent(he, &bp, ep);
			}
			RES_SET_H_ERRNO(statp, NETDB_SUCCESS);
			return (0);
#endif
		case T_A:
		case T_AAAA:
			if (strcasecmp(he->h_name, bp) != 0) {
				syslog(LOG_NOTICE|LOG_AUTH,
				       AskedForGot, he->h_name, bp);
				cp += n;
				continue;	/* XXX - had_error++ ? */
			}
			if (n != he->h_length) {
				cp += n;
				continue;
			}
			if (!haveanswer) {
				int nn;

				he->h_name = bp;
				nn = strlen(bp) + 1;	/* for the \0 */
				bp += nn;
			}

			bp += sizeof(align) - ((u_long)bp % sizeof(align));

			if (bp + n >= ep) {
				dbg_printf("size (%d) too big\n", n, statp);
				had_error++;
				continue;
			}
			if (hap >= &hed->h_addr_ptrs[_MAXADDRS-1]) {
				if (!toobig++)
					dbg_printf("Too many addresses (%d)\n",
						_MAXADDRS, statp);
				cp += n;
				continue;
			}
			memcpy(*hap++ = bp, cp, n);
			bp += n;
			cp += n;
			if (cp != erdata) {
				RES_SET_H_ERRNO(statp, NO_RECOVERY);
				return (-1);
			}
			break;
		default:
			dbg_printf("Impossible condition (type=%d)\n", type,
			    statp);
			RES_SET_H_ERRNO(statp, NO_RECOVERY);
			return (-1);
			/* BIND has abort() here, too risky on bad data */
		}
		if (!had_error)
			haveanswer++;
	}
	if (haveanswer) {
		*ap = NULL;
		*hap = NULL;
# if defined(RESOLVSORT)
		/*
		 * Note: we sort even if host can take only one address
		 * in its return structures - should give it the "best"
		 * address in that case, not some random one
		 */
		if (statp->nsort && haveanswer > 1 && qtype == T_A)
			addrsort(hed->h_addr_ptrs, haveanswer, statp);
# endif /*RESOLVSORT*/
		if (!he->h_name) {
			n = strlen(qname) + 1;	/* for the \0 */
			if (n > ep - bp || n >= MAXHOSTNAMELEN)
				goto no_recovery;
			strcpy(bp, qname);
			he->h_name = bp;
			bp += n;
		}
		if (statp->options & RES_USE_INET6)
			_map_v4v6_hostent(he, &bp, ep);
		RES_SET_H_ERRNO(statp, NETDB_SUCCESS);
		return (0);
	}
 no_recovery:
	RES_SET_H_ERRNO(statp, NO_RECOVERY);
	return (-1);
}

/* XXX: for async DNS resolver in ypserv */
struct hostent *
__dns_getanswer(const char *answer, int anslen, const char *qname, int qtype)
{
	struct hostent *he;
	struct hostent_data *hed;
	int error;
	res_state statp;

	statp = __res_state();
	if ((he = __hostent_init()) == NULL ||
	    (hed = __hostent_data_init()) == NULL) {
		RES_SET_H_ERRNO(statp, NETDB_INTERNAL);
		return (NULL);
	}
	switch (qtype) {
	case T_AAAA:
		he->h_addrtype = AF_INET6;
		he->h_length = NS_IN6ADDRSZ;
		break;
	case T_A:
	default:
		he->h_addrtype = AF_INET;
		he->h_length = NS_INADDRSZ;
		break;
	}

	error = gethostanswer((const querybuf *)answer, anslen, qname, qtype,
	    he, hed, statp);
	return (error == 0) ? he : NULL;
}

int
_dns_gethostbyname(void *rval, void *cb_data, va_list ap)
{
	const char *name;
	int af;
	char *buffer;
	size_t buflen;
	int *errnop, *h_errnop;
	struct hostent *hptr, he;
	struct hostent_data *hed;
	querybuf *buf;
	int n, type, error;
	res_state statp;

	name = va_arg(ap, const char *);
	af = va_arg(ap, int);
	hptr = va_arg(ap, struct hostent *);
	buffer = va_arg(ap, char *);
	buflen = va_arg(ap, size_t);
	errnop = va_arg(ap, int *);
	h_errnop = va_arg(ap, int *);

	*((struct hostent **)rval) = NULL;

	statp = __res_state();
	if ((hed = __hostent_data_init()) == NULL) {
		RES_SET_H_ERRNO(statp, NETDB_INTERNAL);
		*h_errnop = statp->res_h_errno;
		return (NS_NOTFOUND);
	}

	he.h_addrtype = af;
	switch (af) {
	case AF_INET:
		he.h_length = NS_INADDRSZ;
		type = T_A;
		break;
	case AF_INET6:
		he.h_length = NS_IN6ADDRSZ;
		type = T_AAAA;
		break;
	default:
		RES_SET_H_ERRNO(statp, NETDB_INTERNAL);
		*h_errnop = statp->res_h_errno;
		errno = EAFNOSUPPORT;
		return (NS_UNAVAIL);
	}

	if ((buf = malloc(sizeof(*buf))) == NULL) {
		RES_SET_H_ERRNO(statp, NETDB_INTERNAL);
		*h_errnop = statp->res_h_errno;
		return (NS_NOTFOUND);
	}
	n = res_nsearch(statp, name, C_IN, type, buf->buf, sizeof(buf->buf));
	if (n < 0) {
		free(buf);
		dbg_printf("res_nsearch failed (%d)\n", n, statp);
		*h_errnop = statp->res_h_errno;
		return (NS_NOTFOUND);
	} else if (n > sizeof(buf->buf)) {
		free(buf);
		dbg_printf("static buffer is too small (%d)\n", n, statp);
		*h_errnop = statp->res_h_errno;
		return (NS_UNAVAIL);
	}
	error = gethostanswer(buf, n, name, type, &he, hed, statp);
	free(buf);
	if (error != 0) {
		*h_errnop = statp->res_h_errno;
		switch (statp->res_h_errno) {
		case HOST_NOT_FOUND:
			return (NS_NOTFOUND);
		case TRY_AGAIN:
			return (NS_TRYAGAIN);
		default:
			return (NS_UNAVAIL);
		}
		/*NOTREACHED*/
	}
	if (__copy_hostent(&he, hptr, buffer, buflen) != 0) {
		*errnop = errno;
		RES_SET_H_ERRNO(statp, NETDB_INTERNAL);
		*h_errnop = statp->res_h_errno;
		return (NS_RETURN);
	}
	RES_SET_H_ERRNO(statp, NETDB_SUCCESS);
	*((struct hostent **)rval) = hptr;
	return (NS_SUCCESS);
}

int
_dns_gethostbyaddr(void *rval, void *cb_data, va_list ap)
{
	const void *addr;
	socklen_t len;
	int af;
	char *buffer;
	size_t buflen;
	int *errnop, *h_errnop;
	const u_char *uaddr;
	struct hostent *hptr, he;
	struct hostent_data *hed;
	int n;
	querybuf *buf;
	char qbuf[MAXDNAME+1], *qp;
	res_state statp;
#ifdef SUNSECURITY
	struct hostdata rhd;
	struct hostent *rhe;
	char **haddr;
	u_long old_options;
	char hname2[MAXDNAME+1], numaddr[46];
	int ret_h_error;
#endif /*SUNSECURITY*/

	addr = va_arg(ap, const void *);
	len = va_arg(ap, socklen_t);
	af = va_arg(ap, int);
	hptr = va_arg(ap, struct hostent *);
	buffer = va_arg(ap, char *);
	buflen = va_arg(ap, size_t);
	errnop = va_arg(ap, int *);
	h_errnop = va_arg(ap, int *);
	uaddr = (const u_char *)addr;

	*((struct hostent **)rval) = NULL;

	statp = __res_state();
	if ((hed = __hostent_data_init()) == NULL) {
		RES_SET_H_ERRNO(statp, NETDB_INTERNAL);
		*h_errnop = statp->res_h_errno;
		return (NS_NOTFOUND);
	}

	switch (af) {
	case AF_INET:
		(void) sprintf(qbuf, "%u.%u.%u.%u.in-addr.arpa",
			       (uaddr[3] & 0xff),
			       (uaddr[2] & 0xff),
			       (uaddr[1] & 0xff),
			       (uaddr[0] & 0xff));
		break;
	case AF_INET6:
		qp = qbuf;
		for (n = NS_IN6ADDRSZ - 1; n >= 0; n--) {
			qp += SPRINTF((qp, "%x.%x.",
				       uaddr[n] & 0xf,
				       (uaddr[n] >> 4) & 0xf));
		}
		strlcat(qbuf, "ip6.arpa", sizeof(qbuf));
		break;
	default:
		abort();
	}
	if ((buf = malloc(sizeof(*buf))) == NULL) {
		RES_SET_H_ERRNO(statp, NETDB_INTERNAL);
		*h_errnop = statp->res_h_errno;
		return NS_NOTFOUND;
	}
	n = res_nquery(statp, qbuf, C_IN, T_PTR, (u_char *)buf->buf,
	    sizeof buf->buf);
	if (n < 0) {
		free(buf);
		dbg_printf("res_nquery failed (%d)\n", n, statp);
		*h_errnop = statp->res_h_errno;
		return (NS_UNAVAIL);
	}
	if (n > sizeof buf->buf) {
		free(buf);
		dbg_printf("static buffer is too small (%d)\n", n, statp);
		*h_errnop = statp->res_h_errno;
		return (NS_UNAVAIL);
	}
	if (gethostanswer(buf, n, qbuf, T_PTR, &he, hed, statp) != 0) {
		free(buf);
		*h_errnop = statp->res_h_errno;
		switch (statp->res_h_errno) {
		case HOST_NOT_FOUND:
			return (NS_NOTFOUND);
		case TRY_AGAIN:
			return (NS_TRYAGAIN);
		default:
			return (NS_UNAVAIL);
		}
		/*NOTREACHED*/
	}
	free(buf);
#ifdef SUNSECURITY
	if (af == AF_INET) {
	    /*
	     * turn off search as the name should be absolute,
	     * 'localhost' should be matched by defnames
	     */
	    strncpy(hname2, he.h_name, MAXDNAME);
	    hname2[MAXDNAME] = '\0';
	    old_options = statp->options;
	    statp->options &= ~RES_DNSRCH;
	    statp->options |= RES_DEFNAMES;
	    memset(&rhd, 0, sizeof rhd);
	    rhe = gethostbyname_r(hname2, &rhd.host, &rhd.data,
	        sizeof(rhd.data), &ret_h_error);
	    if (rhe == NULL) {
		if (inet_ntop(af, addr, numaddr, sizeof(numaddr)) == NULL)
		    strlcpy(numaddr, "UNKNOWN", sizeof(numaddr));
		syslog(LOG_NOTICE|LOG_AUTH,
		       "gethostbyaddr: No A record for %s (verifying [%s])",
		       hname2, numaddr);
		statp->options = old_options;
		RES_SET_H_ERRNO(statp, HOST_NOT_FOUND);
		*h_errnop = statp->res_h_errno;
		return (NS_NOTFOUND);
	    }
	    statp->options = old_options;
	    for (haddr = rhe->h_addr_list; *haddr; haddr++)
		if (!memcmp(*haddr, addr, NS_INADDRSZ))
			break;
	    if (!*haddr) {
		if (inet_ntop(af, addr, numaddr, sizeof(numaddr)) == NULL)
		    strlcpy(numaddr, "UNKNOWN", sizeof(numaddr));
		syslog(LOG_NOTICE|LOG_AUTH,
		       "gethostbyaddr: A record of %s != PTR record [%s]",
		       hname2, numaddr);
		RES_SET_H_ERRNO(statp, HOST_NOT_FOUND);
		*h_errnop = statp->res_h_errno;
		return (NS_NOTFOUND);
	    }
	}
#endif /*SUNSECURITY*/
	he.h_addrtype = af;
	he.h_length = len;
	memcpy(hed->host_addr, uaddr, len);
	hed->h_addr_ptrs[0] = (char *)hed->host_addr;
	hed->h_addr_ptrs[1] = NULL;
	if (af == AF_INET && (statp->options & RES_USE_INET6)) {
		_map_v4v6_address((char*)hed->host_addr, (char*)hed->host_addr);
		he.h_addrtype = AF_INET6;
		he.h_length = NS_IN6ADDRSZ;
	}
	if (__copy_hostent(&he, hptr, buffer, buflen) != 0) {
		*errnop = errno;
		RES_SET_H_ERRNO(statp, NETDB_INTERNAL);
		*h_errnop = statp->res_h_errno;
		return (NS_RETURN);
	}
	RES_SET_H_ERRNO(statp, NETDB_SUCCESS);
	*((struct hostent **)rval) = hptr;
	return (NS_SUCCESS);
}

#ifdef RESOLVSORT
static void
addrsort(char **ap, int num, res_state res)
{
	int i, j;
	char **p;
	short aval[_MAXADDRS];
	int needsort = 0;

	p = ap;
	for (i = 0; i < num; i++, p++) {
	    for (j = 0 ; (unsigned)j < res->nsort; j++)
		if (res->sort_list[j].addr.s_addr == 
		    (((struct in_addr *)(*p))->s_addr & res->sort_list[j].mask))
			break;
	    aval[i] = j;
	    if (needsort == 0 && i > 0 && j < aval[i-1])
		needsort = i;
	}
	if (!needsort)
	    return;

	while (needsort < num) {
	    for (j = needsort - 1; j >= 0; j--) {
		if (aval[j] > aval[j+1]) {
		    char *hp;

		    i = aval[j];
		    aval[j] = aval[j+1];
		    aval[j+1] = i;

		    hp = ap[j];
		    ap[j] = ap[j+1];
		    ap[j+1] = hp;

		} else
		    break;
	    }
	    needsort++;
	}
}
#endif

void
_sethostdnsent(int stayopen)
{
	res_state statp;

	statp = __res_state();
	if ((statp->options & RES_INIT) == 0 && res_ninit(statp) == -1)
		return;
	if (stayopen)
		statp->options |= RES_STAYOPEN | RES_USEVC;
}

void
_endhostdnsent(void)
{
	res_state statp;

	statp = __res_state();
	statp->options &= ~(RES_STAYOPEN | RES_USEVC);
	res_nclose(statp);
}
