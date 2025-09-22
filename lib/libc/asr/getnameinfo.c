/*	$OpenBSD: getnameinfo.c,v 1.11 2022/12/27 17:10:06 jmc Exp $	*/
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
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#include <asr.h>
#include <errno.h>
#include <resolv.h>
#include <string.h>

static size_t asr_print_addr(const struct sockaddr *, char *, size_t);
static size_t asr_print_port(const struct sockaddr *, const char *, char *, size_t);

#define SA_IN(sa) ((struct sockaddr_in*)(sa))
#define SA_IN6(sa) ((struct sockaddr_in6*)(sa))

/*
 * Print the textual representation (as given by inet_ntop(3)) of the address
 * set in "sa".
 *
 * Return the total length of the string it tried to create or 0 if an error
 * occurred, in which case errno is set.  On success, the constructed string
 * is guaranteed to be NUL-terminated.  Overflow must be detected by checking
 * the returned size against buflen.
 *
 */
static size_t
asr_print_addr(const struct sockaddr *sa, char *buf, size_t buflen)
{
	unsigned int ifidx;
	char tmp[sizeof("ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255")];
	char scope[IF_NAMESIZE + 1], *ifname;
	const void *addr;
	size_t s;

	switch(sa->sa_family) {
	case AF_INET:
		addr = &SA_IN(sa)->sin_addr;
		break;
	case AF_INET6:
		addr = &SA_IN6(sa)->sin6_addr;
		break;
	default:
		errno = EINVAL;
		return (0);
	}

	if (inet_ntop(sa->sa_family, addr, tmp, sizeof(tmp)) == NULL)
		return (0); /* errno set */

	s = strlcpy(buf, tmp, buflen);

	if (sa->sa_family == AF_INET6 && SA_IN6(sa)->sin6_scope_id) {

		scope[0] = SCOPE_DELIMITER;
		scope[1] = '\0';

		ifidx = SA_IN6(sa)->sin6_scope_id;
		ifname = NULL;

		if (IN6_IS_ADDR_LINKLOCAL(&(SA_IN6(sa)->sin6_addr)) ||
		    IN6_IS_ADDR_MC_LINKLOCAL(&(SA_IN6(sa)->sin6_addr)) ||
		    IN6_IS_ADDR_MC_INTFACELOCAL(&(SA_IN6(sa)->sin6_addr)))
			ifname = if_indextoname(ifidx, scope + 1);

		if (ifname == NULL)
			(void)snprintf(scope + 1, sizeof(scope) - 1, "%u", ifidx);

		if (s < buflen)
			(void)strlcat(buf, scope, buflen);

		s += strlen(scope);
	}

	return (s);
}

/* 
 * Print the textual representation of the port set on "sa".
 *
 * If proto is not NULL, it is used as parameter to "getservbyport_r(3)" to
 * return a service name. If it's not set, or if no matching service is found,
 * it prints the portno.
 *
 * Return the total length of the string it tried to create or 0 if an error
 * occurred, in which case errno is set.  On success, the constructed string
 * is guaranteed to be NUL-terminated.  Overflow must be detected by checking
 * the returned size against buflen.
 */
static size_t
asr_print_port(const struct sockaddr *sa, const char *proto, char *buf, size_t buflen)
{
	struct servent s;
	struct servent_data sd;
	int port, r, saved_errno;
	size_t n;

	switch(sa->sa_family) {
	case AF_INET:
		port = SA_IN(sa)->sin_port;
		break;
	case AF_INET6:
		port = SA_IN6(sa)->sin6_port;
		break;
	default:
		errno = EINVAL;
		return (0);
	}

	if (proto) {
		memset(&sd, 0, sizeof (sd));
		saved_errno = errno;
		r = getservbyport_r(port, proto, &s, &sd);
		if (r == 0)
			n = strlcpy(buf, s.s_name, buflen);
		endservent_r(&sd);
		errno = saved_errno;
		if (r == 0)
			return (n);
	}

	r = snprintf(buf, buflen, "%u", ntohs(port));
	if (r < 0 || r >= buflen) 	/* Actually, this can not happen */
		return (0);

	return (r);
}

int
getnameinfo(const struct sockaddr *sa, socklen_t salen, char *host,
    size_t hostlen, char *serv, size_t servlen, int flags)
{
	struct asr_query *as;
	struct asr_result ar;
	int saved_errno = errno;
	const char *proto;
	size_t r;

	/*
	 * Take a shortcut if we don't care about hostname,
	 * or if NI_NUMERICHOST is set.
	 */
	if (host == NULL || hostlen == 0 ||
	    (host && hostlen && (flags & NI_NUMERICHOST))) {
		if (host) {
			r = asr_print_addr(sa, host, hostlen);
			if (r == 0)
				return (EAI_SYSTEM); /* errno set */
			if (r >= hostlen)
				return (EAI_OVERFLOW);
		}

		if (serv && servlen) {
			if (flags & NI_NUMERICSERV)
				proto = NULL;
			else
				proto = (flags & NI_DGRAM) ? "udp" : "tcp";
			r = asr_print_port(sa, proto, serv, servlen);
			if (r == 0)
				return (EAI_SYSTEM); /* errno set */
			if (r >= servlen)
				return (EAI_OVERFLOW);
		}

		errno = saved_errno;
		return (0);
	}

	res_init();

	as = getnameinfo_async(sa, salen, host, hostlen, serv, servlen, flags,
	    NULL);
	if (as == NULL) {
		if (errno == ENOMEM) {
			errno = saved_errno;
			return (EAI_MEMORY);
		}
		return (EAI_SYSTEM);
	}

	asr_run_sync(as, &ar);
	if (ar.ar_gai_errno == EAI_SYSTEM)
		errno = ar.ar_errno;

	return (ar.ar_gai_errno);
}
DEF_WEAK(getnameinfo);
