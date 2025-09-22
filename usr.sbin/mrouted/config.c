/*	$NetBSD: config.c,v 1.6 1995/12/10 10:06:58 mycroft Exp $	*/

/*
 * The mrouted program is covered by the license in the accompanying file
 * named "LICENSE".  Use of the mrouted program represents acceptance of
 * the terms and conditions listed in that file.
 *
 * The mrouted program is COPYRIGHT 1989 by The Board of Trustees of
 * Leland Stanford Junior University.
 */

#include "defs.h"
#include <net/if.h>
#include <ifaddrs.h>


/*
 * Query the kernel to find network interfaces that are multicast-capable
 * and install them in the uvifs array.
 */
void
config_vifs_from_kernel(void)
{
    struct ifaddrs *ifa, *ifap;
    struct uvif *v;
    vifi_t vifi;
    u_int32_t addr, mask, subnet;
    short flags;

    if (getifaddrs(&ifap) == -1)
	logit(LOG_ERR, errno, "getifaddrs");

    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
	/*
	 * Ignore any interface for an address family other than IP.
	 */
	if (ifa->ifa_addr->sa_family != AF_INET)
	    continue;

	addr = ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr;

	/*
	 * Ignore loopback interfaces and interfaces that do not support
	 * multicast.
	 */
	flags = ifa->ifa_flags;
	if ((flags & (IFF_LOOPBACK|IFF_MULTICAST)) != IFF_MULTICAST)
	    continue;

	/*
	 * Ignore any interface whose address and mask do not define a
	 * valid subnet number, or whose address is of the form {subnet,0}
	 * or {subnet,-1}.
	 */
	mask = ((struct sockaddr_in *)ifa->ifa_netmask)->sin_addr.s_addr;
	subnet = addr & mask;
	if (!inet_valid_subnet(subnet, mask) ||
	    addr == subnet ||
	    addr == (subnet | ~mask)) {
	    logit(LOG_WARNING, 0,
		"ignoring %s, has invalid address (%s) and/or mask (%s)",
		ifa->ifa_name, inet_fmt(addr, s1), inet_fmt(mask, s2));
	    continue;
	}

	/*
	 * Ignore any interface that is connected to the same subnet as
	 * one already installed in the uvifs array.
	 */
	for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v) {
	    if ((addr & v->uv_subnetmask) == v->uv_subnet ||
		(v->uv_subnet & mask) == subnet) {
		logit(LOG_WARNING, 0, "ignoring %s, same subnet as %s",
		    ifa->ifa_name, v->uv_name);
		break;
	    }
	}
	if (vifi != numvifs)
	    continue;

	/*
	 * If there is room in the uvifs array, install this interface.
	 */
	if (numvifs == MAXVIFS) {
	    logit(LOG_WARNING, 0, "too many vifs, ignoring %s", ifa->ifa_name);
	    continue;
	}
	v  = &uvifs[numvifs];
	v->uv_flags       = 0;
	v->uv_metric      = DEFAULT_METRIC;
	v->uv_rate_limit  = DEFAULT_PHY_RATE_LIMIT;
	v->uv_threshold   = DEFAULT_THRESHOLD;
	v->uv_lcl_addr    = addr;
	v->uv_rmt_addr    = 0;
	v->uv_subnet      = subnet;
	v->uv_subnetmask  = mask;
	v->uv_subnetbcast = subnet | ~mask;
	strlcpy(v->uv_name, ifa->ifa_name, sizeof(v->uv_name));
	v->uv_groups      = NULL;
	v->uv_neighbors   = NULL;
	v->uv_acl         = NULL;
	v->uv_addrs	  = NULL;

	logit(LOG_INFO,0,"installing %s (%s on subnet %s) as vif #%u - rate=%d",
	    v->uv_name, inet_fmt(addr, s1), inet_fmts(subnet, mask, s2),
	    numvifs, v->uv_rate_limit);

	++numvifs;

	/*
	 * If the interface is not yet up, set the vifs_down flag to
	 * remind us to check again later.
	 */
	if (!(flags & IFF_UP)) {
	    v->uv_flags |= VIFF_DOWN;
	    vifs_down = TRUE;
	}
    }

    freeifaddrs(ifap);
}
