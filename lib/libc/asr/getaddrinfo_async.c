/*	$OpenBSD: getaddrinfo_async.c,v 1.64 2025/09/17 10:47:30 florian Exp $	*/
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
#include <sys/uio.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <net/if.h>
#include <netdb.h>

#include <asr.h>
#include <errno.h>
#include <ifaddrs.h>
#include <resolv.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "asr_private.h"

struct match {
	int family;
	int socktype;
	int protocol;
};

static int getaddrinfo_async_run(struct asr_query *, struct asr_result *);
static int get_port(const char *, const char *, int);
static int iter_family(struct asr_query *, int);
static int addrinfo_add(struct asr_query *, const struct sockaddr *, const char *);
static int addrinfo_from_file(struct asr_query *, int,  FILE *);
static int addrinfo_from_pkt(struct asr_query *, char *, size_t);
static int addrconfig_setup(struct asr_query *);

static const struct match matches[] = {
	{ PF_INET,	SOCK_DGRAM,	IPPROTO_UDP	},
	{ PF_INET,	SOCK_STREAM,	IPPROTO_TCP	},
	{ PF_INET,	SOCK_RAW,	0		},
	{ PF_INET6,	SOCK_DGRAM,	IPPROTO_UDP	},
	{ PF_INET6,	SOCK_STREAM,	IPPROTO_TCP	},
	{ PF_INET6,	SOCK_RAW,	0		},
	{ -1,		0,		0,		},
};

#define MATCH_FAMILY(a, b) ((a) == matches[(b)].family || (a) == PF_UNSPEC)
#define MATCH_PROTO(a, b) ((a) == matches[(b)].protocol || (a) == 0 || matches[(b)].protocol == 0)
/* Do not match SOCK_RAW unless explicitly specified */
#define MATCH_SOCKTYPE(a, b) ((a) == matches[(b)].socktype || ((a) == 0 && \
				matches[(b)].socktype != SOCK_RAW))

enum {
	DOM_INIT,
	DOM_DOMAIN,
	DOM_DONE
};

struct asr_query *
getaddrinfo_async(const char *hostname, const char *servname,
	const struct addrinfo *hints, void *asr)
{
	struct asr_ctx		*ac;
	struct asr_query	*as;

	if (hints == NULL || (hints->ai_flags & AI_NUMERICHOST) == 0)
		ac = _asr_use_resolver(asr);
	else
		ac = _asr_no_resolver();
	if ((as = _asr_async_new(ac, ASR_GETADDRINFO)) == NULL)
		goto abort; /* errno set */
	as->as_run = getaddrinfo_async_run;

	if (hostname) {
		if ((as->as.ai.hostname = strdup(hostname)) == NULL)
			goto abort; /* errno set */
	}
	if (servname && (as->as.ai.servname = strdup(servname)) == NULL)
		goto abort; /* errno set */
	if (hints)
		memmove(&as->as.ai.hints, hints, sizeof *hints);
	else {
		memset(&as->as.ai.hints, 0, sizeof as->as.ai.hints);
		as->as.ai.hints.ai_family = PF_UNSPEC;
		as->as.ai.hints.ai_flags = AI_ADDRCONFIG;
	}

	_asr_ctx_unref(ac);
	return (as);
    abort:
	if (as)
		_asr_async_free(as);
	_asr_ctx_unref(ac);
	return (NULL);
}
DEF_WEAK(getaddrinfo_async);

static int
getaddrinfo_async_run(struct asr_query *as, struct asr_result *ar)
{
	char		 fqdn[MAXDNAME];
	const char	*str;
	struct addrinfo	*ai;
	int		 i, family, r, is_localhost = 0;
	FILE		*f;
	union {
		struct sockaddr		sa;
		struct sockaddr_in	sain;
		struct sockaddr_in6	sain6;
	} sa;

    next:
	switch (as->as_state) {

	case ASR_STATE_INIT:

		/*
		 * First, make sure the parameters are valid.
		 */

		as->as_count = 0;

		if (as->as.ai.hostname == NULL &&
		    as->as.ai.servname == NULL) {
			ar->ar_gai_errno = EAI_NONAME;
			async_set_state(as, ASR_STATE_HALT);
			break;
		}

		if (as->as.ai.hostname && as->as.ai.hostname[0] == '\0') {
			ar->ar_gai_errno = EAI_NODATA;
			async_set_state(as, ASR_STATE_HALT);
			break;
		}

		ai = &as->as.ai.hints;

		if (ai->ai_addrlen ||
		    ai->ai_canonname ||
		    ai->ai_addr ||
		    ai->ai_next) {
			ar->ar_gai_errno = EAI_BADHINTS;
			async_set_state(as, ASR_STATE_HALT);
			break;
		}

		if (ai->ai_flags & ~AI_MASK ||
		    (ai->ai_flags & AI_CANONNAME && ai->ai_flags & AI_FQDN)) {
			ar->ar_gai_errno = EAI_BADFLAGS;
			async_set_state(as, ASR_STATE_HALT);
			break;
		}

		if (ai->ai_family != PF_UNSPEC &&
		    ai->ai_family != PF_INET &&
		    ai->ai_family != PF_INET6) {
			ar->ar_gai_errno = EAI_FAMILY;
			async_set_state(as, ASR_STATE_HALT);
			break;
		}

		if (ai->ai_socktype &&
		    ai->ai_socktype != SOCK_DGRAM  &&
		    ai->ai_socktype != SOCK_STREAM &&
		    ai->ai_socktype != SOCK_RAW) {
			ar->ar_gai_errno = EAI_SOCKTYPE;
			async_set_state(as, ASR_STATE_HALT);
			break;
		}

		if (ai->ai_socktype == SOCK_RAW &&
		    get_port(as->as.ai.servname, NULL, 1) != 0) {
			ar->ar_gai_errno = EAI_SERVICE;
			async_set_state(as, ASR_STATE_HALT);
			break;
		}

		/* Restrict result set to configured address families */
		if (ai->ai_flags & AI_ADDRCONFIG) {
			if (addrconfig_setup(as) == -1) {
				ar->ar_errno = errno;
				ar->ar_gai_errno = EAI_SYSTEM;
				async_set_state(as, ASR_STATE_HALT);
				break;
			}
		}

		/* Make sure there is at least a valid combination */
		for (i = 0; matches[i].family != -1; i++)
			if (MATCH_FAMILY(ai->ai_family, i) &&
			    MATCH_SOCKTYPE(ai->ai_socktype, i) &&
			    MATCH_PROTO(ai->ai_protocol, i))
				break;
		if (matches[i].family == -1) {
			ar->ar_gai_errno = EAI_BADHINTS;
			async_set_state(as, ASR_STATE_HALT);
			break;
		}

		if (ai->ai_protocol == 0 || ai->ai_protocol == IPPROTO_UDP)
			as->as.ai.port_udp = get_port(as->as.ai.servname, "udp",
			    as->as.ai.hints.ai_flags & AI_NUMERICSERV);
		if (ai->ai_protocol == 0 || ai->ai_protocol == IPPROTO_TCP)
			as->as.ai.port_tcp = get_port(as->as.ai.servname, "tcp",
			    as->as.ai.hints.ai_flags & AI_NUMERICSERV);
		if (as->as.ai.port_tcp == -2 || as->as.ai.port_udp == -2 ||
		    (as->as.ai.port_tcp == -1 && as->as.ai.port_udp == -1) ||
		    (ai->ai_protocol && (as->as.ai.port_udp == -1 ||
					 as->as.ai.port_tcp == -1))) {
			ar->ar_gai_errno = EAI_SERVICE;
			async_set_state(as, ASR_STATE_HALT);
			break;
		}

		ar->ar_gai_errno = 0;

		if (!(ai->ai_flags & AI_NUMERICHOST))
			is_localhost = _asr_is_localhost(as->as.ai.hostname);
		/*
		 * If hostname is NULL, "localhost" or falls within the
		 * ".localhost." domain, use local address.
		 * RFC 6761, 6.3:
		 * 3. Name resolution APIs and libraries SHOULD recognize
		 * localhost names as special and SHOULD always return the IP
		 * loopback address for address queries and negative responses
		 * for all other query types.  Name resolution APIs SHOULD NOT
		 * send queries for localhost names to their configured caching
		 * DNS server(s).
		 */
		if (as->as.ai.hostname == NULL || is_localhost) {
			for (family = iter_family(as, 1);
			    family != -1;
			    family = iter_family(as, 0)) {
				/*
				 * We could use statically built sockaddrs for
				 * those, rather than parsing over and over.
				 */
				if (family == PF_INET)
					str = (ai->ai_flags & AI_PASSIVE &&
					    !is_localhost) ? "0.0.0.0" :
					    "127.0.0.1";
				else /* PF_INET6 */
					str = (ai->ai_flags & AI_PASSIVE &&
					    !is_localhost) ? "::" : "::1";
				 /* This can't fail */
				_asr_sockaddr_from_str(&sa.sa, family, str);
				if ((r = addrinfo_add(as, &sa.sa,
				    "localhost."))) {
					ar->ar_gai_errno = r;
					break;
				}
			}
			if (ar->ar_gai_errno == 0 && as->as_count == 0) {
				ar->ar_gai_errno = EAI_NODATA;
			}
			async_set_state(as, ASR_STATE_HALT);
			break;
		}

		/* Try numeric addresses first */
		if (_asr_sockaddr_from_str(&sa.sa, ai->ai_family,
		    as->as.ai.hostname) != -1) {
			if ((r = addrinfo_add(as, &sa.sa, as->as.ai.hostname)))
				ar->ar_gai_errno = r;
		}
		if (ar->ar_gai_errno || as->as_count) {
			async_set_state(as, ASR_STATE_HALT);
			break;
		}

		if (ai->ai_flags & AI_NUMERICHOST) {
			ar->ar_gai_errno = EAI_NONAME;
			async_set_state(as, ASR_STATE_HALT);
			break;
		}

		/* make sure there are no funny characters in hostname */
		if (!hnok_lenient(as->as.ai.hostname)) {
			ar->ar_gai_errno = EAI_FAIL;
			async_set_state(as, ASR_STATE_HALT);
			break;
		}

		async_set_state(as, ASR_STATE_NEXT_DB);
		break;

	case ASR_STATE_NEXT_DB:
		if (_asr_iter_db(as) == -1) {
			async_set_state(as, ASR_STATE_NOT_FOUND);
			break;
		}
		as->as_family_idx = 0;
		async_set_state(as, ASR_STATE_SAME_DB);
		break;

	case ASR_STATE_NEXT_FAMILY:
		as->as_family_idx += 1;
		if (as->as.ai.hints.ai_family != AF_UNSPEC ||
		    AS_FAMILY(as) == -1) {
			/* The family was specified, or we have tried all
			 * families with this DB.
			 */
			if (as->as_count) {
				ar->ar_gai_errno = 0;
				async_set_state(as, ASR_STATE_HALT);
			} else
				async_set_state(as, ASR_STATE_NEXT_DOMAIN);
			break;
		}
		async_set_state(as, ASR_STATE_SAME_DB);
		break;

	case ASR_STATE_NEXT_DOMAIN:
		/* domain search is only for dns */
		if (AS_DB(as) != ASR_DB_DNS) {
			async_set_state(as, ASR_STATE_NEXT_DB);
			break;
		}
		as->as_family_idx = 0;

		free(as->as.ai.fqdn);
		as->as.ai.fqdn = NULL;
		r = _asr_iter_domain(as, as->as.ai.hostname, fqdn, sizeof(fqdn));
		if (r == -1) {
			async_set_state(as, ASR_STATE_NEXT_DB);
			break;
		}
		if (r == 0) {
			ar->ar_gai_errno = EAI_FAIL;
			async_set_state(as, ASR_STATE_HALT);
			break;
		}
		as->as.ai.fqdn = strdup(fqdn);
		if (as->as.ai.fqdn == NULL) {
			ar->ar_gai_errno = EAI_MEMORY;
			async_set_state(as, ASR_STATE_HALT);
			break;
		}

		async_set_state(as, ASR_STATE_SAME_DB);
		break;

	case ASR_STATE_SAME_DB:
		/* query the current DB again */
		switch (AS_DB(as)) {
		case ASR_DB_DNS:
			if (as->as.ai.fqdn == NULL) {
				/* First try, initialize domain iteration */
				as->as_dom_flags = 0;
				as->as_dom_step = DOM_INIT;
				async_set_state(as, ASR_STATE_NEXT_DOMAIN);
				break;
			}

			family = (as->as.ai.hints.ai_family == AF_UNSPEC) ?
			    AS_FAMILY(as) : as->as.ai.hints.ai_family;

			if (family == AF_INET &&
			    as->as_flags & ASYNC_NO_INET) {
				async_set_state(as, ASR_STATE_NEXT_FAMILY);
				break;
			} else if (family == AF_INET6 &&
			    as->as_flags & ASYNC_NO_INET6) {
				async_set_state(as, ASR_STATE_NEXT_FAMILY);
				break;
			}

			as->as_subq = _res_query_async_ctx(as->as.ai.fqdn,
			    C_IN, (family == AF_INET6) ? T_AAAA : T_A,
			    as->as_ctx);

			if (as->as_subq == NULL) {
				if (errno == ENOMEM)
					ar->ar_gai_errno = EAI_MEMORY;
				else
					ar->ar_gai_errno = EAI_FAIL;
				async_set_state(as, ASR_STATE_HALT);
				break;
			}
			async_set_state(as, ASR_STATE_SUBQUERY);
			break;

		case ASR_DB_FILE:
			f = fopen(_PATH_HOSTS, "re");
			if (f == NULL) {
				async_set_state(as, ASR_STATE_NEXT_DB);
				break;
			}
			family = (as->as.ai.hints.ai_family == AF_UNSPEC) ?
			    AS_FAMILY(as) : as->as.ai.hints.ai_family;

			r = addrinfo_from_file(as, family, f);
			if (r == -1) {
				if (errno == ENOMEM)
					ar->ar_gai_errno = EAI_MEMORY;
				else
					ar->ar_gai_errno = EAI_FAIL;
				async_set_state(as, ASR_STATE_HALT);
			} else
				async_set_state(as, ASR_STATE_NEXT_FAMILY);
			fclose(f);
			break;

		default:
			async_set_state(as, ASR_STATE_NEXT_DB);
		}
		break;

	case ASR_STATE_SUBQUERY:
		if ((r = asr_run(as->as_subq, ar)) == ASYNC_COND)
			return (ASYNC_COND);

		as->as_subq = NULL;

		if (ar->ar_datalen == -1) {
			async_set_state(as, ASR_STATE_NEXT_FAMILY);
			break;
		}

		r = addrinfo_from_pkt(as, ar->ar_data, ar->ar_datalen);
		if (r == -1) {
			if (errno == ENOMEM)
				ar->ar_gai_errno = EAI_MEMORY;
			else
				ar->ar_gai_errno = EAI_FAIL;
			async_set_state(as, ASR_STATE_HALT);
		} else
			async_set_state(as, ASR_STATE_NEXT_FAMILY);
		free(ar->ar_data);
		break;

	case ASR_STATE_NOT_FOUND:
		/* No result found. Maybe we can try again. */
		if (as->as_flags & ASYNC_AGAIN)
			ar->ar_gai_errno = EAI_AGAIN;
		else
			ar->ar_gai_errno = EAI_NODATA;
		async_set_state(as, ASR_STATE_HALT);
		break;

	case ASR_STATE_HALT:
		if (ar->ar_gai_errno == 0) {
			ar->ar_count = as->as_count;
			ar->ar_addrinfo = as->as.ai.aifirst;
			as->as.ai.aifirst = NULL;
		} else {
			ar->ar_count = 0;
			ar->ar_addrinfo = NULL;
		}
		return (ASYNC_DONE);

	default:
		ar->ar_errno = EOPNOTSUPP;
		ar->ar_gai_errno = EAI_SYSTEM;
		async_set_state(as, ASR_STATE_HALT);
		break;
	}
	goto next;
}

/*
 * Retrieve the port number for the service name "servname" and
 * the protocol "proto".
 */
static int
get_port(const char *servname, const char *proto, int numonly)
{
	struct servent		se;
	struct servent_data	sed;
	int			port;
	const char		*e;

	if (servname == NULL)
		return (0);

	e = NULL;
	port = strtonum(servname, 0, USHRT_MAX, &e);
	if (e == NULL)
		return (port);
	if (errno == ERANGE)
		return (-2); /* invalid */
	if (numonly)
		return (-2);

	port = -1;
	memset(&sed, 0, sizeof(sed));
	if (getservbyname_r(servname, proto, &se, &sed) != -1)
		port = ntohs(se.s_port);
	endservent_r(&sed);

	return (port);
}

/*
 * Iterate over the address families that are to be queried. Use the
 * list on the async context, unless a specific family was given in hints.
 */
static int
iter_family(struct asr_query *as, int first)
{
	if (first) {
		as->as_family_idx = 0;
		if (as->as.ai.hints.ai_family != PF_UNSPEC)
			return as->as.ai.hints.ai_family;
		return AS_FAMILY(as);
	}

	if (as->as.ai.hints.ai_family != PF_UNSPEC)
		return (-1);

	as->as_family_idx++;

	return AS_FAMILY(as);
}

/*
 * Use the sockaddr at "sa" to extend the result list on the "as" context,
 * with the specified canonical name "cname". This function adds one
 * entry per protocol/socktype match.
 */
static int
addrinfo_add(struct asr_query *as, const struct sockaddr *sa, const char *cname)
{
	struct addrinfo		*ai;
	int			 i, port, proto;

	for (i = 0; matches[i].family != -1; i++) {
		if (matches[i].family != sa->sa_family ||
		    !MATCH_SOCKTYPE(as->as.ai.hints.ai_socktype, i) ||
		    !MATCH_PROTO(as->as.ai.hints.ai_protocol, i))
			continue;

		proto = as->as.ai.hints.ai_protocol;
		if (!proto)
			proto = matches[i].protocol;

		if (proto == IPPROTO_TCP)
			port = as->as.ai.port_tcp;
		else if (proto == IPPROTO_UDP)
			port = as->as.ai.port_udp;
		else
			port = 0;

		/* servname specified, but not defined for this protocol */
		if (port == -1)
			continue;

		ai = calloc(1, sizeof(*ai) + sa->sa_len);
		if (ai == NULL)
			return (EAI_MEMORY);
		ai->ai_family = sa->sa_family;
		ai->ai_socktype = matches[i].socktype;
		ai->ai_protocol = proto;
		ai->ai_flags = as->as.ai.hints.ai_flags;
		ai->ai_addrlen = sa->sa_len;
		ai->ai_addr = (void *)(ai + 1);
		if (cname &&
		    as->as.ai.hints.ai_flags & (AI_CANONNAME | AI_FQDN)) {
			if ((ai->ai_canonname = strdup(cname)) == NULL) {
				free(ai);
				return (EAI_MEMORY);
			}
		}
		memmove(ai->ai_addr, sa, sa->sa_len);
		if (sa->sa_family == PF_INET)
			((struct sockaddr_in *)ai->ai_addr)->sin_port =
			    htons(port);
		else if (sa->sa_family == PF_INET6)
			((struct sockaddr_in6 *)ai->ai_addr)->sin6_port =
			    htons(port);

		if (as->as.ai.aifirst == NULL)
			as->as.ai.aifirst = ai;
		if (as->as.ai.ailast)
			as->as.ai.ailast->ai_next = ai;
		as->as.ai.ailast = ai;
		as->as_count += 1;
	}

	return (0);
}

static int
addrinfo_from_file(struct asr_query *as, int family, FILE *f)
{
	char		*tokens[MAXTOKEN], *c, buf[BUFSIZ + 1];
	int		 n, i;
	union {
		struct sockaddr		sa;
		struct sockaddr_in	sain;
		struct sockaddr_in6	sain6;
	} u;

	for (;;) {
		n = _asr_parse_namedb_line(f, tokens, MAXTOKEN, buf, sizeof(buf));
		if (n == -1)
			break; /* ignore errors reading the file */

		for (i = 1; i < n; i++) {
			if (strcasecmp(as->as.ai.hostname, tokens[i]))
				continue;
			if (_asr_sockaddr_from_str(&u.sa, family, tokens[0]) == -1)
				continue;
			break;
		}
		if (i == n)
			continue;

		if (as->as.ai.hints.ai_flags & (AI_CANONNAME | AI_FQDN))
			c = tokens[1];
		else
			c = NULL;

		if (addrinfo_add(as, &u.sa, c))
			return (-1); /* errno set */
	}
	return (0);
}

static int
addrinfo_from_pkt(struct asr_query *as, char *pkt, size_t pktlen)
{
	struct asr_unpack	 p;
	struct asr_dns_header	 h;
	struct asr_dns_query	 q;
	struct asr_dns_rr	 rr;
	int			 i;
	union {
		struct sockaddr		sa;
		struct sockaddr_in	sain;
		struct sockaddr_in6	sain6;
	} u;
	char		 buf[MAXDNAME], *c;

	_asr_unpack_init(&p, pkt, pktlen);
	_asr_unpack_header(&p, &h);
	for (; h.qdcount; h.qdcount--)
		_asr_unpack_query(&p, &q);

	for (i = 0; i < h.ancount; i++) {
		_asr_unpack_rr(&p, &rr);
		if (rr.rr_type != q.q_type ||
		    rr.rr_class != q.q_class)
			continue;

		memset(&u, 0, sizeof u);
		if (rr.rr_type == T_A) {
			u.sain.sin_len = sizeof u.sain;
			u.sain.sin_family = AF_INET;
			u.sain.sin_addr = rr.rr.in_a.addr;
			u.sain.sin_port = 0;
		} else if (rr.rr_type == T_AAAA) {
			u.sain6.sin6_len = sizeof u.sain6;
			u.sain6.sin6_family = AF_INET6;
			u.sain6.sin6_addr = rr.rr.in_aaaa.addr6;
			u.sain6.sin6_port = 0;
		} else
			continue;

		if (as->as.ai.hints.ai_flags & AI_CANONNAME) {
			_asr_strdname(rr.rr_dname, buf, sizeof buf);
			buf[strlen(buf) - 1] = '\0';
			c = res_hnok(buf) ? buf : as->as.ai.hostname;
		} else if (as->as.ai.hints.ai_flags & AI_FQDN)
			c = as->as.ai.fqdn;
		else
			c = NULL;

		if (addrinfo_add(as, &u.sa, c))
			return (-1); /* errno set */
	}
	return (0);
}

static int
addrconfig_setup(struct asr_query *as)
{
	struct ifaddrs		*ifa, *ifa0;
	struct if_data		*ifa_data;
	struct sockaddr_in	*sinp;
	struct sockaddr_in6	*sin6p;
	int			 rtable, ifa_rtable = -1;

	if (getifaddrs(&ifa0) == -1)
		return (-1);

	rtable = getrtable();

	as->as_flags |= ASYNC_NO_INET | ASYNC_NO_INET6;

	for (ifa = ifa0; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL)
			continue;

		switch (ifa->ifa_addr->sa_family) {
		case PF_LINK:
			/* AF_LINK comes before inet / inet6 on an interface */
			ifa_data = (struct if_data *)ifa->ifa_data;
			ifa_rtable = ifa_data->ifi_rdomain;
			break;
		case PF_INET:
			if (ifa_rtable != rtable)
				continue;

			sinp = (struct sockaddr_in *)ifa->ifa_addr;

			if (sinp->sin_addr.s_addr == htonl(INADDR_LOOPBACK))
				continue;

			as->as_flags &= ~ASYNC_NO_INET;
			break;
		case PF_INET6:
			if (ifa_rtable != rtable)
				continue;

			sin6p = (struct sockaddr_in6 *)ifa->ifa_addr;

			if (IN6_IS_ADDR_LOOPBACK(&sin6p->sin6_addr))
				continue;

			if (IN6_IS_ADDR_LINKLOCAL(&sin6p->sin6_addr))
				continue;

			as->as_flags &= ~ASYNC_NO_INET6;
			break;
		}
	}

	freeifaddrs(ifa0);

	return (0);
}
