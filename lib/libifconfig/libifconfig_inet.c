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

static const struct sockaddr_in NULL_SIN;

static int
inet_prefixlen(const struct in_addr *addr)
{
	int x;

	x = ffs(ntohl(addr->s_addr));
	return (x == 0 ? 0 : 33 - x);
}

int
ifconfig_inet_get_addrinfo(ifconfig_handle_t *h __unused,
    const char *name __unused, struct ifaddrs *ifa,
    struct ifconfig_inet_addr *addr)
{
	bzero(addr, sizeof(*addr));

	/* Set the address */
	if (ifa->ifa_addr == NULL) {
		return (-1);
	} else {
		addr->sin = (struct sockaddr_in *)ifa->ifa_addr;
	}

	/* Set the destination address */
	if (ifa->ifa_flags & IFF_POINTOPOINT) {
		if (ifa->ifa_dstaddr) {
			addr->dst = (struct sockaddr_in *)ifa->ifa_dstaddr;
		} else {
			addr->dst = &NULL_SIN;
		}
	}

	/* Set the netmask and prefixlen */
	if (ifa->ifa_netmask) {
		addr->netmask = (struct sockaddr_in *)ifa->ifa_netmask;
	} else {
		addr->netmask = &NULL_SIN;
	}
	addr->prefixlen = inet_prefixlen(&addr->netmask->sin_addr);

	/* Set the broadcast */
	if (ifa->ifa_flags & IFF_BROADCAST) {
		addr->broadcast = (struct sockaddr_in *)ifa->ifa_broadaddr;
	}

	/* Set the vhid */
	if (ifa->ifa_data) {
		addr->vhid = ((struct if_data *)ifa->ifa_data)->ifi_vhid;
	}

	return (0);
}
