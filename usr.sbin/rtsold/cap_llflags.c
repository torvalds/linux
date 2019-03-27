/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2018 The FreeBSD Foundation
 *
 * This software was developed by Mark Johnston under sponsorship from
 * the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the distribution.
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

#include <sys/types.h>
#include <sys/dnv.h>
#include <sys/ioctl.h>
#include <sys/nv.h>
#include <sys/queue.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet6/in6_var.h>

#include <errno.h>
#include <ifaddrs.h>
#include <string.h>

#include <libcasper.h>
#include <libcasper_service.h>

#include "rtsold.h"

/*
 * A service to fetch the flags for the link-local IPv6 address on the specified
 * interface.  This cannot easily be done in capability mode because we need to
 * use the routing socket sysctl API to find the link-local address of a
 * particular interface.  The SIOCGIFCONF ioctl is one other option, but as
 * currently implemented it is less flexible (it cannot report the required
 * buffer length), and hard-codes a buffer length limit.
 */

static int
llflags_get(const char *ifname, int *flagsp)
{
	struct in6_ifreq ifr6;
	struct ifaddrs *ifap, *ifa;
	struct sockaddr_in6 *sin6;
	int error, s;

	s = socket(PF_INET6, SOCK_DGRAM, 0);
	if (s < 0)
		return (-1);

	if (getifaddrs(&ifap) != 0)
		return (-1);
	error = -1;
	for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
		if (strcmp(ifa->ifa_name, ifname) != 0)
			continue;
		if (ifa->ifa_addr->sa_family != AF_INET6)
			continue;

		sin6 = (struct sockaddr_in6 *)(void *)ifa->ifa_addr;
		if (!IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr))
			continue;

		memset(&ifr6, 0, sizeof(ifr6));
		if (strlcpy(ifr6.ifr_name, ifname, sizeof(ifr6.ifr_name)) >=
		    sizeof(ifr6.ifr_name)) {
			freeifaddrs(ifap);
			errno = EINVAL;
			return (-1);
		}
		memcpy(&ifr6.ifr_ifru.ifru_addr, sin6, sin6->sin6_len);
		if (ioctl(s, SIOCGIFAFLAG_IN6, &ifr6) < 0) {
			error = errno;
			freeifaddrs(ifap);
			errno = error;
			return (-1);
		}

		*flagsp = ifr6.ifr_ifru.ifru_flags6;
		error = 0;
		break;
	}
	(void)close(s);
	freeifaddrs(ifap);
	if (error == -1)
		errno = ENOENT;
	return (error);
}

int
cap_llflags_get(cap_channel_t *cap, const char *ifname, int *flagsp)
{
#ifdef WITH_CASPER
	nvlist_t *nvl;
	int error;

	nvl = nvlist_create(0);
	nvlist_add_string(nvl, "cmd", "get");
	nvlist_add_string(nvl, "ifname", ifname);
	nvl = cap_xfer_nvlist(cap, nvl);
	if (nvl == NULL)
		return (-1);
	error = (int)dnvlist_get_number(nvl, "error", 0);
	if (error == 0)
		*flagsp = (int)nvlist_get_number(nvl, "flags");
	nvlist_destroy(nvl);
	if (error != 0)
		errno = error;
	return (error == 0 ? 0 : -1);
#else
	(void)cap;
	return (llflags_get(ifname, flagsp));
#endif
}

#ifdef WITH_CASPER
static int
llflags_command(const char *cmd, const nvlist_t *limits __unused,
    nvlist_t *nvlin, nvlist_t *nvlout)
{
	const char *ifname;
	int flags;

	if (strcmp(cmd, "get") != 0)
		return (EINVAL);
	ifname = nvlist_get_string(nvlin, "ifname");
	if (llflags_get(ifname, &flags) != 0)
		return (errno);
	nvlist_add_number(nvlout, "flags", flags);
	return (0);
}

CREATE_SERVICE("rtsold.llflags", NULL, llflags_command, 0);
#endif /* WITH_CASPER */
