/*
 * Copyright (c) 1990 Jan-Simon Pendry
 * Copyright (c) 1990 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry at Imperial College, London.
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
 *	from: @(#)wire.c	8.1 (Berkeley) 6/6/93
 *	$Id: wire.c,v 1.14 2022/12/28 21:30:15 jmc Exp $
 */

/*
 * This function returns the subnet (address&netmask) for the primary network
 * interface.  If the resulting address has an entry in the hosts file, the
 * corresponding name is returned, otherwise the address is returned in
 * standard internet format.
 * As a side-effect, a list of local IP/net address is recorded for use
 * by the islocalnet() function.
 *
 * Derived from original by Paul Anderson (23/4/90)
 * Updates from Dirk Grunwald (11/11/91)
 * Modified to use getifaddrs() by Todd C. Miller (6/14/2003)
 */

#include "am.h"

#include <ifaddrs.h>
#include <netdb.h>
#include <net/if.h>

#define NO_SUBNET "notknown"

/*
 * List of locally connected networks
 */
typedef struct addrlist addrlist;
struct addrlist {
	addrlist *ip_next;
	in_addr_t ip_addr;
	in_addr_t ip_mask;
};
static addrlist *localnets = 0;

char *
getwire(void)
{
	struct ifaddrs *ifa, *ifaddrs;
	struct hostent *hp;
	addrlist *al;
	char *s, *netname = NULL;

	if (getifaddrs(&ifaddrs))
		return strdup(NO_SUBNET);

	for (ifa = ifaddrs; ifa != NULL; ifa = ifa -> ifa_next) {
		/*
		 * Ignore non-AF_INET interfaces as well as any that
		 * are down or loopback.
		 */
		if (ifa->ifa_addr == NULL ||
		    ifa->ifa_addr->sa_family != AF_INET ||
		    !(ifa->ifa_flags & IFF_UP) ||
		    (ifa->ifa_flags & IFF_LOOPBACK))
			continue;
		
		/*
		 * Add interface to local network list
		 */
		al = ALLOC(addrlist);
		al->ip_addr =
		    ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr;
		al->ip_mask =
		    ((struct sockaddr_in *)ifa->ifa_netmask)->sin_addr.s_addr;
		al->ip_next = localnets;
		localnets = al;

		/*
		 * Look up the host name; fall back to a dotted quad.
		 */
		if (netname == NULL) {
			in_addr_t subnet;
			char dq[20];

			subnet = al->ip_addr & al->ip_mask;
			hp = gethostbyaddr((char *) &subnet, 4, AF_INET);
			if (hp)
				s = hp->h_name;
			else
				s = inet_dquad(dq, sizeof(dq), subnet);
			netname = strdup(s);
		}
	}
	freeifaddrs(ifaddrs);
	return (netname ? netname : strdup(NO_SUBNET));
}

/*
 * Determine whether a network is on a local network
 * (addr) is in network byte order.
 */
int
islocalnet(in_addr_t addr)
{
	addrlist *al;

	for (al = localnets; al; al = al->ip_next)
		if (((addr ^ al->ip_addr) & al->ip_mask) == 0)
			return TRUE;

#ifdef DEBUG
	{ char buf[16];
	plog(XLOG_INFO, "%s is on a remote network", inet_dquad(buf, sizeof(buf), addr));
	}
#endif
	return FALSE;
}
