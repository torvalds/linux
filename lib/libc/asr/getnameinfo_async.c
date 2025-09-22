/*	$OpenBSD: getnameinfo_async.c,v 1.15 2020/12/21 09:40:35 eric Exp $	*/
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
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <netdb.h>

#include <asr.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "asr_private.h"

static int getnameinfo_async_run(struct asr_query *, struct asr_result *);
static int _servname(struct asr_query *);
static int _numerichost(struct asr_query *);

struct asr_query *
getnameinfo_async(const struct sockaddr *sa, socklen_t slen, char *host,
    size_t hostlen, char *serv, size_t servlen, int flags, void *asr)
{
	struct asr_ctx	 *ac;
	struct asr_query *as;

	ac = _asr_use_resolver(asr);
	if ((as = _asr_async_new(ac, ASR_GETNAMEINFO)) == NULL)
		goto abort; /* errno set */
	as->as_run = getnameinfo_async_run;

	if (sa->sa_family == AF_INET)
		memmove(&as->as.ni.sa.sa, sa, sizeof (as->as.ni.sa.sain));
	else if (sa->sa_family == AF_INET6)
		memmove(&as->as.ni.sa.sa, sa, sizeof (as->as.ni.sa.sain6));

	as->as.ni.sa.sa.sa_len = slen;
	as->as.ni.hostname = host;
	as->as.ni.hostnamelen = hostlen;
	as->as.ni.servname = serv;
	as->as.ni.servnamelen = servlen;
	as->as.ni.flags = flags;

	_asr_ctx_unref(ac);
	return (as);

    abort:
	if (as)
		_asr_async_free(as);
	_asr_ctx_unref(ac);
	return (NULL);
}
DEF_WEAK(getnameinfo_async);

static int
getnameinfo_async_run(struct asr_query *as, struct asr_result *ar)
{
	void		*addr;
	socklen_t	 addrlen;
	int		 r;

    next:
	switch (as->as_state) {

	case ASR_STATE_INIT:

		/* Make sure the parameters are all valid. */

		if (as->as.ni.sa.sa.sa_family != AF_INET &&
		    as->as.ni.sa.sa.sa_family != AF_INET6) {
			ar->ar_gai_errno = EAI_FAMILY;
			async_set_state(as, ASR_STATE_HALT);
			break;
		}

		if ((as->as.ni.sa.sa.sa_family == AF_INET &&
		    (as->as.ni.sa.sa.sa_len != sizeof (as->as.ni.sa.sain))) ||
		    (as->as.ni.sa.sa.sa_family == AF_INET6 &&
		    (as->as.ni.sa.sa.sa_len != sizeof (as->as.ni.sa.sain6)))) {
			ar->ar_gai_errno = EAI_FAIL;
			async_set_state(as, ASR_STATE_HALT);
			break;
		}

		/* Set the service name first, if needed. */
		if (_servname(as) == -1) {
			ar->ar_gai_errno = EAI_OVERFLOW;
			async_set_state(as, ASR_STATE_HALT);
			break;
		}

		if (as->as.ni.hostname == NULL || as->as.ni.hostnamelen == 0) {
			ar->ar_gai_errno = 0;
			async_set_state(as, ASR_STATE_HALT);
			break;
		}

		if (as->as.ni.flags & NI_NUMERICHOST) {
			if (_numerichost(as) == -1) {
				if (errno == ENOMEM)
					ar->ar_gai_errno = EAI_MEMORY;
				else if (errno == ENOSPC)
					ar->ar_gai_errno = EAI_OVERFLOW;
				else {
					ar->ar_errno = errno;
					ar->ar_gai_errno = EAI_SYSTEM;
				}
			} else
				ar->ar_gai_errno = 0;
			async_set_state(as, ASR_STATE_HALT);
			break;
		}

		if (as->as.ni.sa.sa.sa_family == AF_INET) {
			addrlen = sizeof(as->as.ni.sa.sain.sin_addr);
			addr = &as->as.ni.sa.sain.sin_addr;
		} else {
			addrlen = sizeof(as->as.ni.sa.sain6.sin6_addr);
			addr = &as->as.ni.sa.sain6.sin6_addr;
		}

		/*
		 * Create a subquery to lookup the address.
		 */
		as->as_subq = _gethostbyaddr_async_ctx(addr, addrlen,
		    as->as.ni.sa.sa.sa_family,
		    as->as_ctx);
		if (as->as_subq == NULL) {
			ar->ar_gai_errno = EAI_MEMORY;
			async_set_state(as, ASR_STATE_HALT);
			break;
		}

		async_set_state(as, ASR_STATE_SUBQUERY);
		break;

	case ASR_STATE_SUBQUERY:

		if ((r = asr_run(as->as_subq, ar)) == ASYNC_COND)
			return (ASYNC_COND);

		/*
		 * Request done.
		 */
		as->as_subq = NULL;

		if (ar->ar_hostent == NULL) {
			if (as->as.ni.flags & NI_NAMEREQD) {
				ar->ar_gai_errno = EAI_NONAME;
			} else if (_numerichost(as) == -1) {
				if (errno == ENOMEM)
					ar->ar_gai_errno = EAI_MEMORY;
				else if (errno == ENOSPC)
					ar->ar_gai_errno = EAI_OVERFLOW;
				else {
					ar->ar_errno = errno;
					ar->ar_gai_errno = EAI_SYSTEM;
				}
			} else
				ar->ar_gai_errno = 0;
		} else {
			if (strlcpy(as->as.ni.hostname,
			    ar->ar_hostent->h_name,
			    as->as.ni.hostnamelen) >= as->as.ni.hostnamelen)
				ar->ar_gai_errno = EAI_OVERFLOW;
			else
				ar->ar_gai_errno = 0;
			free(ar->ar_hostent);
		}

		async_set_state(as, ASR_STATE_HALT);
		break;

	case ASR_STATE_HALT:
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
 * Set the service name on the result buffer is not NULL.
 * return (-1) if the buffer is too small.
 */
static int
_servname(struct asr_query *as)
{
	struct servent		 s;
	struct servent_data	 sd;
	int			 port, r;
	char			*buf = as->as.ni.servname;
	size_t			 n, buflen = as->as.ni.servnamelen;

	if (as->as.ni.servname == NULL || as->as.ni.servnamelen == 0)
		return (0);

	if (as->as.ni.sa.sa.sa_family == AF_INET)
		port = as->as.ni.sa.sain.sin_port;
	else
		port = as->as.ni.sa.sain6.sin6_port;

	if (!(as->as.ni.flags & NI_NUMERICSERV)) {
		memset(&sd, 0, sizeof (sd));
		r = getservbyport_r(port, (as->as.ni.flags & NI_DGRAM) ?
		    "udp" : "tcp", &s, &sd);
		if (r == 0)
			n = strlcpy(buf, s.s_name, buflen);
		endservent_r(&sd);
		if (r == 0) {
			if (n >= buflen)
				return (-1);
			return (0);
		}
	}

	r = snprintf(buf, buflen, "%u", ntohs(port));
	if (r < 0 || r >= buflen)
		return (-1);

	return (0);
}

/*
 * Write the numeric address
 */
static int
_numerichost(struct asr_query *as)
{
	unsigned int	ifidx;
	char		scope[IF_NAMESIZE + 1], *ifname;
	void		*addr;
	char		*buf = as->as.ni.hostname;
	size_t		 buflen = as->as.ni.hostnamelen;

	if (as->as.ni.sa.sa.sa_family == AF_INET)
		addr = &as->as.ni.sa.sain.sin_addr;
	else
		addr = &as->as.ni.sa.sain6.sin6_addr;

	if (inet_ntop(as->as.ni.sa.sa.sa_family, addr, buf, buflen) == NULL)
		return (-1); /* errno set */

	if (as->as.ni.sa.sa.sa_family == AF_INET6 &&
	    as->as.ni.sa.sain6.sin6_scope_id) {

		scope[0] = SCOPE_DELIMITER;
		scope[1] = '\0';

		ifidx = as->as.ni.sa.sain6.sin6_scope_id;
		ifname = NULL;

		if (IN6_IS_ADDR_LINKLOCAL(&as->as.ni.sa.sain6.sin6_addr) ||
		    IN6_IS_ADDR_MC_LINKLOCAL(&as->as.ni.sa.sain6.sin6_addr) ||
		    IN6_IS_ADDR_MC_INTFACELOCAL(&as->as.ni.sa.sain6.sin6_addr))
			ifname = if_indextoname(ifidx, scope + 1);

		if (ifname == NULL)
			snprintf(scope + 1, sizeof(scope) - 1, "%u", ifidx);

		strlcat(buf, scope, buflen);
	}

	return (0);
}
