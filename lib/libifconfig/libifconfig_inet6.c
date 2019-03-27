/*
 * Copyright (c) 1983, 1993
 *  The Regents of the University of California.  All rights reserved.
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
 *
 * $FreeBSD$
 */
#include <sys/param.h>
#include <sys/ioctl.h>

#include <net/if.h>
#include <netinet/in.h>

#include <errno.h>
#include <ifaddrs.h>
#include <string.h>
#include <strings.h>

#include "libifconfig.h"
#include "libifconfig_internal.h"


static int
inet6_prefixlen(struct in6_addr *addr)
{
	int prefixlen = 0;
	int i;

	for (i = 0; i < 4; i++) {
		int x = ffs(ntohl(addr->__u6_addr.__u6_addr32[i]));
		prefixlen += x == 0 ? 0 : 33 - x;
		if (x != 1) {
			break;
		}
	}
	return (prefixlen);
}

int
ifconfig_inet6_get_addrinfo(ifconfig_handle_t *h,
    const char *name, struct ifaddrs *ifa, struct ifconfig_inet6_addr *addr)
{
	struct sockaddr_in6 *netmask;
	struct in6_ifreq ifr6;

	bzero(addr, sizeof(*addr));

	/* Set the address */
	addr->sin6 = (struct sockaddr_in6 *)ifa->ifa_addr;

	/* Set the destination address */
	if (ifa->ifa_flags & IFF_POINTOPOINT) {
		addr->dstin6 = (struct sockaddr_in6 *)ifa->ifa_dstaddr;
	}

	/* Set the prefixlen */
	netmask = (struct sockaddr_in6 *)ifa->ifa_netmask;
	addr->prefixlen = inet6_prefixlen(&netmask->sin6_addr);

	/* Set the flags */
	strlcpy(ifr6.ifr_name, name, sizeof(ifr6.ifr_name));
	ifr6.ifr_addr = *addr->sin6;
	if (ifconfig_ioctlwrap(h, AF_INET6, SIOCGIFAFLAG_IN6, &ifr6) < 0) {
		return (-1);
	}
	addr->flags = ifr6.ifr_ifru.ifru_flags6;

	/* Set the lifetimes */
	memset(&addr->lifetime, 0, sizeof(addr->lifetime));
	ifr6.ifr_addr = *addr->sin6;
	if (ifconfig_ioctlwrap(h, AF_INET6, SIOCGIFALIFETIME_IN6, &ifr6) < 0) {
		return (-1);
	}
	addr->lifetime = ifr6.ifr_ifru.ifru_lifetime; /* struct copy */

	/* Set the vhid */
	if (ifa->ifa_data && ifa->ifa_data) {
		addr->vhid = ((struct if_data *)ifa->ifa_data)->ifi_vhid;
	}

	return (0);
}
