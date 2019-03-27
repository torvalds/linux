/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2009 Bruce Simpson.
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <net/if_dl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>

#include <assert.h>
#include <errno.h>
#include <ifaddrs.h>
#include <stdlib.h>
#include <string.h>

#include "un-namespace.h"

/*
 * Advanced (Full-state) multicast group membership APIs [RFC3678]
 * Currently this module assumes IPv4 support (INET) in the base system.
 */
#ifndef INET
#define INET
#endif

union sockunion {
	struct sockaddr_storage	ss;
	struct sockaddr		sa;
	struct sockaddr_dl	sdl;
#ifdef INET
	struct sockaddr_in	sin;
#endif
#ifdef INET6
	struct sockaddr_in6	sin6;
#endif
};
typedef union sockunion sockunion_t;

#ifndef MIN
#define	MIN(a, b)	((a) < (b) ? (a) : (b))
#endif

/*
 * Internal: Map an IPv4 unicast address to an interface index.
 * This is quite inefficient so it is recommended applications use
 * the newer, more portable, protocol independent API.
 */
static uint32_t
__inaddr_to_index(in_addr_t ifaddr)
{
	struct ifaddrs	*ifa;
	struct ifaddrs	*ifaddrs;
	char		*ifname;
	int		 ifindex;
	sockunion_t	*psu;

	if (getifaddrs(&ifaddrs) < 0)
		return (0);

	ifindex = 0;
	ifname = NULL;

	/*
	 * Pass #1: Find the ifaddr entry corresponding to the
	 * supplied IPv4 address. We should really use the ifindex
	 * consistently for matches, however it is not available to
	 * us on this pass.
	 */
	for (ifa = ifaddrs; ifa != NULL; ifa = ifa->ifa_next) {
		psu = (sockunion_t *)ifa->ifa_addr;
		if (psu && psu->ss.ss_family == AF_INET &&
		    psu->sin.sin_addr.s_addr == ifaddr) {
			ifname = ifa->ifa_name;
			break;
		}
	}
	if (ifname == NULL)
		goto out;

	/*
	 * Pass #2: Find the index of the interface matching the name
	 * we obtained from looking up the IPv4 ifaddr in pass #1.
	 * There must be a better way of doing this.
	 */
	for (ifa = ifaddrs; ifa != NULL; ifa = ifa->ifa_next) {
		psu = (sockunion_t *)ifa->ifa_addr;
		if (psu && psu->ss.ss_family == AF_LINK &&
		    strcmp(ifa->ifa_name, ifname) == 0) {
			ifindex = LLINDEX(&psu->sdl);
			break;
		}
	}
	assert(ifindex != 0);

out:
	freeifaddrs(ifaddrs);
	return (ifindex);
}

/*
 * Set IPv4 source filter list in use on socket.
 *
 * Stubbed to setsourcefilter(). Performs conversion of structures which
 * may be inefficient; applications are encouraged to use the
 * protocol-independent API.
 */
int
setipv4sourcefilter(int s, struct in_addr interface, struct in_addr group,
    uint32_t fmode, uint32_t numsrc, struct in_addr *slist)
{
#ifdef INET
	sockunion_t	 tmpgroup;
	struct in_addr	*pina;
	sockunion_t	*psu, *tmpslist;
	int		 err;
	size_t		 i;
	uint32_t	 ifindex;

	assert(s != -1);

	tmpslist = NULL;

	if (!IN_MULTICAST(ntohl(group.s_addr)) ||
	    (fmode != MCAST_INCLUDE && fmode != MCAST_EXCLUDE)) {
		errno = EINVAL;
		return (-1);
	}

	ifindex = __inaddr_to_index(interface.s_addr);
	if (ifindex == 0) {
		errno = EADDRNOTAVAIL;
		return (-1);
	}

	memset(&tmpgroup, 0, sizeof(sockunion_t));
	tmpgroup.sin.sin_family = AF_INET;
	tmpgroup.sin.sin_len = sizeof(struct sockaddr_in);
	tmpgroup.sin.sin_addr = group;

	if (numsrc != 0 || slist != NULL) {
		tmpslist = calloc(numsrc, sizeof(sockunion_t));
		if (tmpslist == NULL) {
			errno = ENOMEM;
			return (-1);
		}

		pina = slist;
		psu = tmpslist;
		for (i = 0; i < numsrc; i++, pina++, psu++) {
			psu->sin.sin_family = AF_INET;
			psu->sin.sin_len = sizeof(struct sockaddr_in);
			psu->sin.sin_addr = *pina;
		}
	}

	err = setsourcefilter(s, ifindex, (struct sockaddr *)&tmpgroup,
	    sizeof(struct sockaddr_in), fmode, numsrc,
	    (struct sockaddr_storage *)tmpslist);

	if (tmpslist != NULL)
		free(tmpslist);

	return (err);
#else /* !INET */
	return (EAFNOSUPPORT);
#endif /* INET */
}

/*
 * Get IPv4 source filter list in use on socket.
 *
 * Stubbed to getsourcefilter(). Performs conversion of structures which
 * may be inefficient; applications are encouraged to use the
 * protocol-independent API.
 * An slist of NULL may be used for guessing the required buffer size.
 */
int
getipv4sourcefilter(int s, struct in_addr interface, struct in_addr group,
    uint32_t *fmode, uint32_t *numsrc, struct in_addr *slist)
{
	sockunion_t	*psu, *tmpslist;
	sockunion_t	 tmpgroup;
	struct in_addr	*pina;
	int		 err;
	size_t		 i;
	uint32_t	 ifindex, onumsrc;

	assert(s != -1);
	assert(fmode != NULL);
	assert(numsrc != NULL);

	onumsrc = *numsrc;
	*numsrc = 0;
	tmpslist = NULL;

	if (!IN_MULTICAST(ntohl(group.s_addr)) ||
	    (onumsrc != 0 && slist == NULL)) {
		errno = EINVAL;
		return (-1);
	}

	ifindex = __inaddr_to_index(interface.s_addr);
	if (ifindex == 0) {
		errno = EADDRNOTAVAIL;
		return (-1);
	}

	memset(&tmpgroup, 0, sizeof(sockunion_t));
	tmpgroup.sin.sin_family = AF_INET;
	tmpgroup.sin.sin_len = sizeof(struct sockaddr_in);
	tmpgroup.sin.sin_addr = group;

	if (onumsrc != 0 || slist != NULL) {
		tmpslist = calloc(onumsrc, sizeof(sockunion_t));
		if (tmpslist == NULL) {
			errno = ENOMEM;
			return (-1);
		}
	}

	err = getsourcefilter(s, ifindex, (struct sockaddr *)&tmpgroup,
	    sizeof(struct sockaddr_in), fmode, numsrc,
	    (struct sockaddr_storage *)tmpslist);

	if (tmpslist != NULL && *numsrc != 0) {
		pina = slist;
		psu = tmpslist;
		for (i = 0; i < MIN(onumsrc, *numsrc); i++, psu++) {
			if (psu->ss.ss_family != AF_INET)
				continue;
			*pina++ = psu->sin.sin_addr;
		}
		free(tmpslist);
	}

	return (err);
}

/*
 * Set protocol-independent source filter list in use on socket.
 */
int
setsourcefilter(int s, uint32_t interface, struct sockaddr *group,
    socklen_t grouplen, uint32_t fmode, uint32_t numsrc,
    struct sockaddr_storage *slist)
{
	struct __msfilterreq	 msfr;
	sockunion_t		*psu;
	int			 level, optname;

	if (fmode != MCAST_INCLUDE && fmode != MCAST_EXCLUDE) {
		errno = EINVAL;
		return (-1);
	}

	psu = (sockunion_t *)group;
	switch (psu->ss.ss_family) {
#ifdef INET
	case AF_INET:
		if ((grouplen != sizeof(struct sockaddr_in) ||
		    !IN_MULTICAST(ntohl(psu->sin.sin_addr.s_addr)))) {
			errno = EINVAL;
			return (-1);
		}
		level = IPPROTO_IP;
		optname = IP_MSFILTER;
		break;
#endif
#ifdef INET6
	case AF_INET6:
		if (grouplen != sizeof(struct sockaddr_in6) ||
		    !IN6_IS_ADDR_MULTICAST(&psu->sin6.sin6_addr)) {
			errno = EINVAL;
			return (-1);
		}
		level = IPPROTO_IPV6;
		optname = IPV6_MSFILTER;
		break;
#endif
	default:
		errno = EAFNOSUPPORT;
		return (-1);
	}

	memset(&msfr, 0, sizeof(msfr));
	msfr.msfr_ifindex = interface;
	msfr.msfr_fmode = fmode;
	msfr.msfr_nsrcs = numsrc;
	memcpy(&msfr.msfr_group, &psu->ss, psu->ss.ss_len);
	msfr.msfr_srcs = slist;		/* pointer */

	return (_setsockopt(s, level, optname, &msfr, sizeof(msfr)));
}

/*
 * Get protocol-independent source filter list in use on socket.
 * An slist of NULL may be used for guessing the required buffer size.
 */
int
getsourcefilter(int s, uint32_t interface, struct sockaddr *group,
    socklen_t grouplen, uint32_t *fmode, uint32_t *numsrc,
    struct sockaddr_storage *slist)
{
	struct __msfilterreq	 msfr;
	sockunion_t		*psu;
	socklen_t		 optlen;
	int			 err, level, nsrcs, optname;

	if (interface == 0 || group == NULL || numsrc == NULL ||
	    fmode == NULL) {
		errno = EINVAL;
		return (-1);
	}

	nsrcs = *numsrc;
	*numsrc = 0;
	*fmode = 0;

	psu = (sockunion_t *)group;
	switch (psu->ss.ss_family) {
#ifdef INET
	case AF_INET:
		if ((grouplen != sizeof(struct sockaddr_in) ||
		    !IN_MULTICAST(ntohl(psu->sin.sin_addr.s_addr)))) {
			errno = EINVAL;
			return (-1);
		}
		level = IPPROTO_IP;
		optname = IP_MSFILTER;
		break;
#endif
#ifdef INET6
	case AF_INET6:
		if (grouplen != sizeof(struct sockaddr_in6) ||
		    !IN6_IS_ADDR_MULTICAST(&psu->sin6.sin6_addr)) {
			errno = EINVAL;
			return (-1);
		}
		level = IPPROTO_IPV6;
		optname = IPV6_MSFILTER;
		break;
#endif
	default:
		errno = EAFNOSUPPORT;
		return (-1);
		break;
	}

	optlen = sizeof(struct __msfilterreq);
	memset(&msfr, 0, optlen);
	msfr.msfr_ifindex = interface;
	msfr.msfr_fmode = 0;
	msfr.msfr_nsrcs = nsrcs;
	memcpy(&msfr.msfr_group, &psu->ss, psu->ss.ss_len);

	/*
	 * msfr_srcs is a pointer to a vector of sockaddr_storage. It
	 * may be NULL. The kernel will always return the total number
	 * of filter entries for the group in msfr.msfr_nsrcs.
	 */
	msfr.msfr_srcs = slist;
	err = _getsockopt(s, level, optname, &msfr, &optlen);
	if (err == 0) {
		*numsrc = msfr.msfr_nsrcs;
		*fmode = msfr.msfr_fmode;
	}

	return (err);
}
