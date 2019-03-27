/*
 * $NetBSD: util.c,v 1.4 2000/08/03 00:04:30 fvdl Exp $
 * $FreeBSD$
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Frank van der Linden.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <net/if.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <sys/poll.h>
#include <rpc/rpc.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <netconfig.h>
#include <stdio.h>
#include <arpa/inet.h>

#include "rpcbind.h"

static struct sockaddr_in *local_in4;
#ifdef INET6
static struct sockaddr_in6 *local_in6;
#endif

static int bitmaskcmp(struct sockaddr *, struct sockaddr *, struct sockaddr *);

/*
 * For all bits set in "mask", compare the corresponding bits in
 * "dst" and "src", and see if they match. Returns 0 if the addresses
 * match.
 */
static int
bitmaskcmp(struct sockaddr *dst, struct sockaddr *src, struct sockaddr *mask)
{
	int i;
	u_int8_t *p1, *p2, *netmask;
	int bytelen;

	if (dst->sa_family != src->sa_family ||
	    dst->sa_family != mask->sa_family)
		return (1);

	switch (dst->sa_family) {
	case AF_INET:
		p1 = (uint8_t*) &SA2SINADDR(dst);
		p2 = (uint8_t*) &SA2SINADDR(src);
		netmask = (uint8_t*) &SA2SINADDR(mask);
		bytelen = sizeof(struct in_addr);
		break;
#ifdef INET6
	case AF_INET6:
		p1 = (uint8_t*) &SA2SIN6ADDR(dst);
		p2 = (uint8_t*) &SA2SIN6ADDR(src);
		netmask = (uint8_t*) &SA2SIN6ADDR(mask);
		bytelen = sizeof(struct in6_addr);
		break;
#endif
	default:
		return (1);
	}

	for (i = 0; i < bytelen; i++)
		if ((p1[i] & netmask[i]) != (p2[i] & netmask[i]))
			return (1);
	return (0);
}

/*
 * Find a server address that can be used by `caller' to contact
 * the local service specified by `serv_uaddr'. If `clnt_uaddr' is
 * non-NULL, it is used instead of `caller' as a hint suggesting
 * the best address (e.g. the `r_addr' field of an rpc, which
 * contains the rpcbind server address that the caller used).
 *
 * Returns the best server address as a malloc'd "universal address"
 * string which should be freed by the caller. On error, returns NULL.
 */
char *
addrmerge(struct netbuf *caller, const char *serv_uaddr, const char *clnt_uaddr,
	  const char *netid)
{
	struct ifaddrs *ifap, *ifp = NULL, *bestif;
	struct netbuf *serv_nbp = NULL, *hint_nbp = NULL, tbuf;
	struct sockaddr *caller_sa, *hint_sa, *ifsa, *ifmasksa, *serv_sa;
	struct sockaddr_storage ss;
	struct netconfig *nconf;
	char *caller_uaddr = NULL;
#ifdef ND_DEBUG
	const char *hint_uaddr = NULL;
#endif
	char *ret = NULL;
	int bestif_goodness;

#ifdef ND_DEBUG
	if (debugging)
		fprintf(stderr, "addrmerge(caller, %s, %s, %s\n", serv_uaddr,
		    clnt_uaddr == NULL ? "NULL" : clnt_uaddr, netid);
#endif
	caller_sa = caller->buf;
	if ((nconf = rpcbind_get_conf(netid)) == NULL)
		goto freeit;
	if ((caller_uaddr = taddr2uaddr(nconf, caller)) == NULL)
		goto freeit;

	/*
	 * Use `clnt_uaddr' as the hint if non-NULL, but ignore it if its
	 * address family is different from that of the caller.
	 */
	hint_sa = NULL;
	if (clnt_uaddr != NULL) {
#ifdef ND_DEBUG
		hint_uaddr = clnt_uaddr;
#endif
		if ((hint_nbp = uaddr2taddr(nconf, clnt_uaddr)) == NULL)
			goto freeit;
		hint_sa = hint_nbp->buf;
	}
	if (hint_sa == NULL || hint_sa->sa_family != caller_sa->sa_family) {
#ifdef ND_DEBUG
		hint_uaddr = caller_uaddr;
#endif
		hint_sa = caller->buf;
	}

#ifdef ND_DEBUG
	if (debugging)
		fprintf(stderr, "addrmerge: hint %s\n", hint_uaddr);
#endif
	/* Local caller, just return the server address. */
	if (strncmp(caller_uaddr, "0.0.0.0.", 8) == 0 ||
	    strncmp(caller_uaddr, "::.", 3) == 0 || caller_uaddr[0] == '/') {
		ret = strdup(serv_uaddr);
		goto freeit;
	}

	if (getifaddrs(&ifp) < 0)
		goto freeit;

	/*
	 * Loop through all interface addresses.  We are listening to an address
	 * if any of the following are true:
	 * a) It's a loopback address
	 * b) It was specified with the -h command line option
	 * c) There were no -h command line options.
	 *
	 * Among addresses on which we are listening, choose in order of
	 * preference an address that is:
	 *
	 * a) Equal to the hint
	 * b) A link local address with the same scope ID as the client's
	 *    address, if the client's address is also link local
	 * c) An address on the same subnet as the client's address
	 * d) A non-localhost, non-p2p address
	 * e) Any usable address
	 */
	bestif = NULL;
	bestif_goodness = 0;
	for (ifap = ifp; ifap != NULL; ifap = ifap->ifa_next) {
		ifsa = ifap->ifa_addr;
		ifmasksa = ifap->ifa_netmask;

		/* Skip addresses where we don't listen */
		if (ifsa == NULL || ifsa->sa_family != hint_sa->sa_family ||
		    !(ifap->ifa_flags & IFF_UP))
			continue;

		if (!(ifap->ifa_flags & IFF_LOOPBACK) && !listen_addr(ifsa))
			continue;

		if ((hint_sa->sa_family == AF_INET) &&
		    ((((struct sockaddr_in*)hint_sa)->sin_addr.s_addr == 
		      ((struct sockaddr_in*)ifsa)->sin_addr.s_addr))) {
			const int goodness = 4;

			bestif_goodness = goodness;
			bestif = ifap;
			goto found;
		}
#ifdef INET6
		if ((hint_sa->sa_family == AF_INET6) &&
		    (0 == memcmp(&((struct sockaddr_in6*)hint_sa)->sin6_addr,
				 &((struct sockaddr_in6*)ifsa)->sin6_addr,
				 sizeof(struct in6_addr))) &&
		    (((struct sockaddr_in6*)hint_sa)->sin6_scope_id ==
		    (((struct sockaddr_in6*)ifsa)->sin6_scope_id))) {
			const int goodness = 4;

			bestif_goodness = goodness;
			bestif = ifap;
			goto found;
		}
		if (hint_sa->sa_family == AF_INET6) {
			/*
			 * For v6 link local addresses, if the caller is on
			 * a link-local address then use the scope id to see
			 * which one.
			 */
			if (IN6_IS_ADDR_LINKLOCAL(&SA2SIN6ADDR(ifsa)) &&
			    IN6_IS_ADDR_LINKLOCAL(&SA2SIN6ADDR(caller_sa)) &&
			    IN6_IS_ADDR_LINKLOCAL(&SA2SIN6ADDR(hint_sa))) {
				if (SA2SIN6(ifsa)->sin6_scope_id ==
				    SA2SIN6(caller_sa)->sin6_scope_id) {
					const int goodness = 3;

					if (bestif_goodness < goodness) {
						bestif = ifap;
						bestif_goodness = goodness;
					}
				}
			}
		}
#endif /* INET6 */
		if (0 == bitmaskcmp(hint_sa, ifsa, ifmasksa)) {
			const int goodness = 2;

			if (bestif_goodness < goodness) {
				bestif = ifap;
				bestif_goodness = goodness;
			}
		}
		if (!(ifap->ifa_flags & (IFF_LOOPBACK | IFF_POINTOPOINT))) {
			const int goodness = 1;

			if (bestif_goodness < goodness) {
				bestif = ifap;
				bestif_goodness = goodness;
			}
		}
		if (bestif == NULL)
			bestif = ifap;
	}
	if (bestif == NULL)
		goto freeit;

found:
	/*
	 * Construct the new address using the address from
	 * `bestif', and the port number from `serv_uaddr'.
	 */
	serv_nbp = uaddr2taddr(nconf, serv_uaddr);
	if (serv_nbp == NULL)
		goto freeit;
	serv_sa = serv_nbp->buf;

	memcpy(&ss, bestif->ifa_addr, bestif->ifa_addr->sa_len);
	switch (ss.ss_family) {
	case AF_INET:
		SA2SIN(&ss)->sin_port = SA2SIN(serv_sa)->sin_port;
		break;
#ifdef INET6
	case AF_INET6:
		SA2SIN6(&ss)->sin6_port = SA2SIN6(serv_sa)->sin6_port;
		break;
#endif
	}
	tbuf.len = ss.ss_len;
	tbuf.maxlen = sizeof(ss);
	tbuf.buf = &ss;
	ret = taddr2uaddr(nconf, &tbuf);

freeit:
	free(caller_uaddr);
	if (hint_nbp != NULL) {
		free(hint_nbp->buf);
		free(hint_nbp);
	}
	if (serv_nbp != NULL) {
		free(serv_nbp->buf);
		free(serv_nbp);
	}
	if (ifp != NULL)
		freeifaddrs(ifp);

#ifdef ND_DEBUG
	if (debugging)
		fprintf(stderr, "addrmerge: returning %s\n", ret);
#endif
	return ret;
}

void
network_init(void)
{
#ifdef INET6
	struct ifaddrs *ifap, *ifp;
	struct ipv6_mreq mreq6;
	unsigned int ifindex;
	int s;
#endif
	int ecode;
	struct addrinfo hints, *res;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	if ((ecode = getaddrinfo(NULL, "sunrpc", &hints, &res))) {
		if (debugging)
			fprintf(stderr, "can't get local ip4 address: %s\n",
			    gai_strerror(ecode));
	} else {
		local_in4 = (struct sockaddr_in *)malloc(sizeof *local_in4);
		if (local_in4 == NULL) {
			if (debugging)
				fprintf(stderr, "can't alloc local ip4 addr\n");
			exit(1);
		}
		memcpy(local_in4, res->ai_addr, sizeof *local_in4);
		freeaddrinfo(res);
	}

#ifdef INET6
	hints.ai_family = AF_INET6;
	if ((ecode = getaddrinfo(NULL, "sunrpc", &hints, &res))) {
		if (debugging)
			fprintf(stderr, "can't get local ip6 address: %s\n",
			    gai_strerror(ecode));
	} else {
		local_in6 = (struct sockaddr_in6 *)malloc(sizeof *local_in6);
		if (local_in6 == NULL) {
			if (debugging)
				fprintf(stderr, "can't alloc local ip6 addr\n");
			exit(1);
		}
		memcpy(local_in6, res->ai_addr, sizeof *local_in6);
		freeaddrinfo(res);
	}

	/*
	 * Now join the RPC ipv6 multicast group on all interfaces.
	 */
	if (getifaddrs(&ifp) < 0)
		return;

	mreq6.ipv6mr_interface = 0;
	inet_pton(AF_INET6, RPCB_MULTICAST_ADDR, &mreq6.ipv6mr_multiaddr);

	s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	if (s == -1) {
		if (debugging)
			fprintf(stderr, "couldn't create ip6 socket");
		goto done_inet6;
	}

	/*
	 * Loop through all interfaces. For each IPv6 multicast-capable
	 * interface, join the RPC multicast group on that interface.
	 */
	for (ifap = ifp; ifap != NULL; ifap = ifap->ifa_next) {
		if (ifap->ifa_addr->sa_family != AF_INET6 ||
		    !(ifap->ifa_flags & IFF_MULTICAST))
			continue;
		ifindex = if_nametoindex(ifap->ifa_name);
		if (ifindex == mreq6.ipv6mr_interface)
			/*
			 * Already did this one.
			 */
			continue;
		mreq6.ipv6mr_interface = ifindex;
		if (setsockopt(s, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq6,
		    sizeof mreq6) < 0)
			if (debugging)
				perror("setsockopt v6 multicast");
	}
done_inet6:
	freeifaddrs(ifp);
#endif

	/* close(s); */
}

struct sockaddr *
local_sa(int af)
{
	switch (af) {
	case AF_INET:
		return (struct sockaddr *)local_in4;
#ifdef INET6
	case AF_INET6:
		return (struct sockaddr *)local_in6;
#endif
	default:
		return NULL;
	}
}
