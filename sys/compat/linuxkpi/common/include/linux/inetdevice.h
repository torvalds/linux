/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013-2017 Mellanox Technologies, Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef	_LINUX_INETDEVICE_H_
#define	_LINUX_INETDEVICE_H_

#include <linux/netdevice.h>

static inline struct net_device *
ip_dev_find(struct vnet *vnet, uint32_t addr)
{
	struct sockaddr_in sin;
	struct epoch_tracker et;
	struct ifaddr *ifa;
	struct ifnet *ifp;

	memset(&sin, 0, sizeof(sin));
	sin.sin_addr.s_addr = addr;
	sin.sin_len = sizeof(sin);
	sin.sin_family = AF_INET;
	NET_EPOCH_ENTER(et);
	CURVNET_SET_QUIET(vnet);
	ifa = ifa_ifwithaddr((struct sockaddr *)&sin);
	CURVNET_RESTORE();
	if (ifa) {
		ifp = ifa->ifa_ifp;
		if_ref(ifp);
	} else {
		ifp = NULL;
	}
	NET_EPOCH_EXIT(et);
	return (ifp);
}

static inline struct net_device *
ip6_dev_find(struct vnet *vnet, struct in6_addr addr, uint16_t scope_id)
{
	struct sockaddr_in6 sin6;
	struct epoch_tracker et;
	struct ifaddr *ifa;
	struct ifnet *ifp;

	memset(&sin6, 0, sizeof(sin6));
	sin6.sin6_addr = addr;
	sin6.sin6_len = sizeof(sin6);
	sin6.sin6_family = AF_INET6;
	if (IN6_IS_SCOPE_LINKLOCAL(&addr) ||
	    IN6_IS_ADDR_MC_INTFACELOCAL(&addr)) {
		/* embed the IPv6 scope ID */
		sin6.sin6_addr.s6_addr16[1] = htons(scope_id);
	}
	NET_EPOCH_ENTER(et);
	CURVNET_SET_QUIET(vnet);
	ifa = ifa_ifwithaddr((struct sockaddr *)&sin6);
	CURVNET_RESTORE();
	if (ifa != NULL) {
		ifp = ifa->ifa_ifp;
		if_ref(ifp);
	} else {
		ifp = NULL;
	}
	NET_EPOCH_EXIT(et);
	return (ifp);
}

#endif	/* _LINUX_INETDEVICE_H_ */
