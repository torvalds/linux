/*
 * Copyright (c) 1983, 1993
 *  The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2016-2017, Marie Helene Kvello-Aune.  All rights reserved.
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

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_mib.h>
#include <netinet/in.h>
#include <netinet6/in6_var.h>
#include <netinet6/nd6.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <net/if_vlan_var.h>

#include "libifconfig.h"
#include "libifconfig_internal.h"

#define NOTAG    ((u_short) -1)

static bool
isnd6defif(ifconfig_handle_t *h, const char *name)
{
	struct in6_ndifreq ndifreq;
	unsigned int ifindex;

	memset(&ndifreq, 0, sizeof(ndifreq));
	strlcpy(ndifreq.ifname, name, sizeof(ndifreq.ifname));
	ifindex = if_nametoindex(ndifreq.ifname);
	if (ifconfig_ioctlwrap(h, AF_INET6, SIOCGDEFIFACE_IN6, &ndifreq) < 0) {
		return (false);
	}
	h->error.errtype = OK;
	return (ndifreq.ifindex == ifindex);
}

ifconfig_handle_t *
ifconfig_open(void)
{
	ifconfig_handle_t *h;

	h = calloc(1, sizeof(*h));

	if (h == NULL) {
		return (NULL);
	}
	for (int i = 0; i <= AF_MAX; i++) {
		h->sockets[i] = -1;
	}

	return (h);
}

void
ifconfig_close(ifconfig_handle_t *h)
{

	for (int i = 0; i <= AF_MAX; i++) {
		if (h->sockets[i] != -1) {
			(void)close(h->sockets[i]);
		}
	}
	freeifaddrs(h->ifap);
	free(h);
}

ifconfig_errtype
ifconfig_err_errtype(ifconfig_handle_t *h)
{

	return (h->error.errtype);
}

int
ifconfig_err_errno(ifconfig_handle_t *h)
{

	return (h->error.errcode);
}

unsigned long
ifconfig_err_ioctlreq(ifconfig_handle_t *h)
{

	return (h->error.ioctl_request);
}

int
ifconfig_foreach_iface(ifconfig_handle_t *h,
    ifconfig_foreach_func_t cb, void *udata)
{
	int ret;

	ret = ifconfig_getifaddrs(h);
	if (ret == 0) {
		struct ifaddrs *ifa;
		char *ifname = NULL;

		for (ifa = h->ifap; ifa; ifa = ifa->ifa_next) {
			if (ifname != ifa->ifa_name) {
				ifname = ifa->ifa_name;
				cb(h, ifa, udata);
			}
		}
	}
	/* Free ifaddrs so we don't accidentally cache stale data */
	freeifaddrs(h->ifap);
	h->ifap = NULL;

	return (ret);
}

void
ifconfig_foreach_ifaddr(ifconfig_handle_t *h, struct ifaddrs *ifa,
    ifconfig_foreach_func_t cb, void *udata)
{
	struct ifaddrs *ift;

	for (ift = ifa;
	    ift != NULL &&
	    ift->ifa_addr != NULL &&
	    strcmp(ift->ifa_name, ifa->ifa_name) == 0;
	    ift = ift->ifa_next) {
		cb(h, ift, udata);
	}
}

int
ifconfig_get_description(ifconfig_handle_t *h, const char *name,
    char **description)
{
	struct ifreq ifr;
	char *descr;
	size_t descrlen;

	descr = NULL;
	descrlen = 64;
	memset(&ifr, 0, sizeof(ifr));
	(void)strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));

	for (;;) {
		if ((descr = reallocf(descr, descrlen)) == NULL) {
			h->error.errtype = OTHER;
			h->error.errcode = ENOMEM;
			return (-1);
		}

		ifr.ifr_buffer.buffer = descr;
		ifr.ifr_buffer.length = descrlen;
		if (ifconfig_ioctlwrap(h, AF_LOCAL, SIOCGIFDESCR, &ifr) != 0) {
			free(descr);
			return (-1);
		}

		if (ifr.ifr_buffer.buffer == descr) {
			if (strlen(descr) > 0) {
				*description = strdup(descr);
				free(descr);

				if (description == NULL) {
					h->error.errtype = OTHER;
					h->error.errcode = ENOMEM;
					return (-1);
				}

				return (0);
			}
		} else if (ifr.ifr_buffer.length > descrlen) {
			descrlen = ifr.ifr_buffer.length;
			continue;
		}
		break;
	}
	free(descr);
	h->error.errtype = OTHER;
	h->error.errcode = 0;
	return (-1);
}

int
ifconfig_set_description(ifconfig_handle_t *h, const char *name,
    const char *newdescription)
{
	struct ifreq ifr;
	int desclen;

	memset(&ifr, 0, sizeof(ifr));
	desclen = strlen(newdescription);

	/*
	 * Unset description if the new description is 0 characters long.
	 * TODO: Decide whether this should be an error condition instead.
	 */
	if (desclen == 0) {
		return (ifconfig_unset_description(h, name));
	}

	(void)strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
	ifr.ifr_buffer.length = desclen + 1;
	ifr.ifr_buffer.buffer = strdup(newdescription);

	if (ifr.ifr_buffer.buffer == NULL) {
		h->error.errtype = OTHER;
		h->error.errcode = ENOMEM;
		return (-1);
	}

	if (ifconfig_ioctlwrap(h, AF_LOCAL, SIOCSIFDESCR, &ifr) != 0) {
		free(ifr.ifr_buffer.buffer);
		return (-1);
	}

	free(ifr.ifr_buffer.buffer);
	return (0);
}

int
ifconfig_unset_description(ifconfig_handle_t *h, const char *name)
{
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));
	(void)strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
	ifr.ifr_buffer.length = 0;
	ifr.ifr_buffer.buffer = NULL;

	if (ifconfig_ioctlwrap(h, AF_LOCAL, SIOCSIFDESCR, &ifr) < 0) {
		return (-1);
	}
	return (0);
}

int
ifconfig_set_name(ifconfig_handle_t *h, const char *name, const char *newname)
{
	struct ifreq ifr;
	char *tmpname;

	memset(&ifr, 0, sizeof(ifr));
	tmpname = strdup(newname);
	if (tmpname == NULL) {
		h->error.errtype = OTHER;
		h->error.errcode = ENOMEM;
		return (-1);
	}

	(void)strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
	ifr.ifr_data = tmpname;
	if (ifconfig_ioctlwrap(h, AF_LOCAL, SIOCSIFNAME, &ifr) != 0) {
		free(tmpname);
		return (-1);
	}

	free(tmpname);
	return (0);
}

int
ifconfig_get_orig_name(ifconfig_handle_t *h, const char *ifname,
    char **orig_name)
{
	size_t len;
	unsigned int ifindex;
	int name[6];

	ifindex = if_nametoindex(ifname);
	if (ifindex == 0) {
		goto fail;
	}

	name[0] = CTL_NET;
	name[1] = PF_LINK;
	name[2] = NETLINK_GENERIC;
	name[3] = IFMIB_IFDATA;
	name[4] = ifindex;
	name[5] = IFDATA_DRIVERNAME;

	len = 0;
	if (sysctl(name, 6, NULL, &len, 0, 0) < 0) {
		goto fail;
	}

	*orig_name = malloc(len);
	if (*orig_name == NULL) {
		goto fail;
	}

	if (sysctl(name, 6, *orig_name, &len, 0, 0) < 0) {
		free(*orig_name);
		*orig_name = NULL;
		goto fail;
	}

	return (0);

fail:
	h->error.errtype = OTHER;
	h->error.errcode = (errno != 0) ? errno : ENOENT;
	return (-1);
}

int
ifconfig_get_fib(ifconfig_handle_t *h, const char *name, int *fib)
{
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));
	(void)strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));

	if (ifconfig_ioctlwrap(h, AF_LOCAL, SIOCGIFFIB, &ifr) == -1) {
		return (-1);
	}

	*fib = ifr.ifr_fib;
	return (0);
}

int
ifconfig_set_mtu(ifconfig_handle_t *h, const char *name, const int mtu)
{
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));
	(void)strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
	ifr.ifr_mtu = mtu;

	if (ifconfig_ioctlwrap(h, AF_LOCAL, SIOCSIFMTU, &ifr) < 0) {
		return (-1);
	}

	return (0);
}

int
ifconfig_get_mtu(ifconfig_handle_t *h, const char *name, int *mtu)
{
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));
	(void)strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));

	if (ifconfig_ioctlwrap(h, AF_LOCAL, SIOCGIFMTU, &ifr) == -1) {
		return (-1);
	}

	*mtu = ifr.ifr_mtu;
	return (0);
}

int
ifconfig_get_nd6(ifconfig_handle_t *h, const char *name,
    struct in6_ndireq *nd)
{
	memset(nd, 0, sizeof(*nd));
	strlcpy(nd->ifname, name, sizeof(nd->ifname));
	if (ifconfig_ioctlwrap(h, AF_INET6, SIOCGIFINFO_IN6, nd) == -1) {
		return (-1);
	}
	if (isnd6defif(h, name)) {
		nd->ndi.flags |= ND6_IFF_DEFAULTIF;
	} else if (h->error.errtype != OK) {
		return (-1);
	}

	return (0);
}

int
ifconfig_set_metric(ifconfig_handle_t *h, const char *name, const int metric)
{
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));
	(void)strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
	ifr.ifr_metric = metric;

	if (ifconfig_ioctlwrap(h, AF_LOCAL, SIOCSIFMETRIC, &ifr) < 0) {
		return (-1);
	}

	return (0);
}

int
ifconfig_get_metric(ifconfig_handle_t *h, const char *name, int *metric)
{
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));
	(void)strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));

	if (ifconfig_ioctlwrap(h, AF_LOCAL, SIOCGIFMETRIC, &ifr) == -1) {
		return (-1);
	}

	*metric = ifr.ifr_metric;
	return (0);
}

int
ifconfig_set_capability(ifconfig_handle_t *h, const char *name,
    const int capability)
{
	struct ifreq ifr;
	struct ifconfig_capabilities ifcap;
	int flags, value;

	memset(&ifr, 0, sizeof(ifr));

	if (ifconfig_get_capability(h, name, &ifcap) != 0) {
		return (-1);
	}

	value = capability;
	flags = ifcap.curcap;
	if (value < 0) {
		value = -value;
		flags &= ~value;
	} else {
		flags |= value;
	}
	flags &= ifcap.reqcap;

	(void)strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));

	/*
	 * TODO: Verify that it's safe to not have ifr.ifr_curcap
	 * set for this request.
	 */
	ifr.ifr_reqcap = flags;
	if (ifconfig_ioctlwrap(h, AF_LOCAL, SIOCSIFCAP, &ifr) < 0) {
		return (-1);
	}
	return (0);
}

int
ifconfig_get_capability(ifconfig_handle_t *h, const char *name,
    struct ifconfig_capabilities *capability)
{
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));
	(void)strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));

	if (ifconfig_ioctlwrap(h, AF_LOCAL, SIOCGIFCAP, &ifr) < 0) {
		return (-1);
	}
	capability->curcap = ifr.ifr_curcap;
	capability->reqcap = ifr.ifr_reqcap;
	return (0);
}

int
ifconfig_get_groups(ifconfig_handle_t *h, const char *name,
    struct ifgroupreq *ifgr)
{
	int len;

	memset(ifgr, 0, sizeof(*ifgr));
	strlcpy(ifgr->ifgr_name, name, IFNAMSIZ);

	if (ifconfig_ioctlwrap(h, AF_LOCAL, SIOCGIFGROUP, ifgr) == -1) {
		if ((h->error.errcode == EINVAL) ||
		    (h->error.errcode == ENOTTY)) {
			return (0);
		} else {
			return (-1);
		}
	}

	len = ifgr->ifgr_len;
	ifgr->ifgr_groups = (struct ifg_req *)malloc(len);
	if (ifgr->ifgr_groups == NULL) {
		return (1);
	}
	bzero(ifgr->ifgr_groups, len);
	if (ifconfig_ioctlwrap(h, AF_LOCAL, SIOCGIFGROUP, ifgr) == -1) {
		return (-1);
	}

	return (0);
}

int
ifconfig_get_ifstatus(ifconfig_handle_t *h, const char *name,
    struct ifstat *ifs)
{
	strlcpy(ifs->ifs_name, name, sizeof(ifs->ifs_name));
	return (ifconfig_ioctlwrap(h, AF_LOCAL, SIOCGIFSTATUS, ifs));
}

int
ifconfig_destroy_interface(ifconfig_handle_t *h, const char *name)
{
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));
	(void)strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));

	if (ifconfig_ioctlwrap(h, AF_LOCAL, SIOCIFDESTROY, &ifr) < 0) {
		return (-1);
	}
	return (0);
}

int
ifconfig_create_interface(ifconfig_handle_t *h, const char *name, char **ifname)
{
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));

	(void)strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));

	/*
	 * TODO:
	 * Insert special snowflake handling here. See GitHub issue #12 for details.
	 * In the meantime, hard-nosupport interfaces that need special handling.
	 */
	if ((strncmp(name, "wlan",
	    strlen("wlan")) == 0) ||
	    (strncmp(name, "vlan",
	    strlen("vlan")) == 0) ||
	    (strncmp(name, "vxlan",
	    strlen("vxlan")) == 0)) {
		h->error.errtype = OTHER;
		h->error.errcode = ENOSYS;
		return (-1);
	}

	/* No special handling for this interface type. */
	if (ifconfig_ioctlwrap(h, AF_LOCAL, SIOCIFCREATE2, &ifr) < 0) {
		return (-1);
	}

	*ifname = strdup(ifr.ifr_name);
	if (ifname == NULL) {
		h->error.errtype = OTHER;
		h->error.errcode = ENOMEM;
		return (-1);
	}

	return (0);
}

int
ifconfig_create_interface_vlan(ifconfig_handle_t *h, const char *name,
    char **ifname, const char *vlandev, const unsigned short vlantag)
{
	struct ifreq ifr;
	struct vlanreq params;

	if ((vlantag == NOTAG) || (vlandev[0] == '\0')) {
		// TODO: Add proper error tracking here
		return (-1);
	}

	bzero(&params, sizeof(params));
	(void)strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
	params.vlr_tag = vlantag;
	(void)strlcpy(params.vlr_parent, vlandev, sizeof(params.vlr_parent));
	ifr.ifr_data = (caddr_t)&params;

	if (ifconfig_ioctlwrap(h, AF_LOCAL, SIOCIFCREATE2, &ifr) < 0) {
		// TODO: Add proper error tracking here
		return (-1);
	}

	*ifname = strdup(ifr.ifr_name);
	return (0);
}

int
ifconfig_set_vlantag(ifconfig_handle_t *h, const char *name,
    const char *vlandev, const unsigned short vlantag)
{
	struct ifreq ifr;
	struct vlanreq params;

	bzero(&params, sizeof(params));
	params.vlr_tag = vlantag;
	strlcpy(params.vlr_parent, vlandev, sizeof(params.vlr_parent));

	ifr.ifr_data = (caddr_t)&params;
	(void)strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
	if (ifconfig_ioctlwrap(h, AF_LOCAL, SIOCSETVLAN, &ifr) == -1) {
		return (-1);
	}
	return (0);
}
