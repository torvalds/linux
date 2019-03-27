/*	$KAME: probe.c,v 1.17 2003/10/05 00:09:36 itojun Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1998 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
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
#include <sys/capsicum.h>
#include <sys/dnv.h>
#include <sys/nv.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_dl.h>

#include <netinet/in.h>
#include <netinet6/in6_var.h>
#include <netinet/icmp6.h>
#include <netinet6/nd6.h>

#include <arpa/inet.h>

#include <capsicum_helpers.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <libcasper.h>
#include <libcasper_service.h>

#include "rtsold.h"

static int
getsocket(int *sockp, int proto)
{
	cap_rights_t rights;
	int sock;

	if (*sockp >= 0)
		return (0);

	if ((sock = socket(AF_INET6, SOCK_RAW, proto)) < 0)
		return (-1);
	cap_rights_init(&rights, CAP_CONNECT, CAP_SEND);
	if (caph_rights_limit(sock, &rights) != 0)
		return (-1);
	*sockp = sock;

	return (0);
}

static ssize_t
sendpacket(int sock, struct sockaddr_in6 *dst, uint32_t ifindex, int hoplimit,
    const void *data, size_t len)
{
	uint8_t cmsg[CMSG_SPACE(sizeof(struct in6_pktinfo)) +
	    CMSG_SPACE(sizeof(int))];
	struct msghdr hdr;
	struct iovec iov;
	struct in6_pktinfo *pi;
	struct cmsghdr *cm;

	memset(&hdr, 0, sizeof(hdr));
	hdr.msg_name = dst;
	hdr.msg_namelen = sizeof(*dst);
	hdr.msg_iov = &iov;
	hdr.msg_iovlen = 1;
	hdr.msg_control = cmsg;
	hdr.msg_controllen = sizeof(cmsg);

	iov.iov_base = __DECONST(void *, data);
	iov.iov_len = len;

	/* Specify the outbound interface. */
	cm = CMSG_FIRSTHDR(&hdr);
	cm->cmsg_level = IPPROTO_IPV6;
	cm->cmsg_type = IPV6_PKTINFO;
	cm->cmsg_len = CMSG_LEN(sizeof(struct in6_pktinfo));
	pi = (struct in6_pktinfo *)(void *)CMSG_DATA(cm);
	memset(&pi->ipi6_addr, 0, sizeof(pi->ipi6_addr));	/*XXX*/
	pi->ipi6_ifindex = ifindex;

	/* Specify the hop limit of the packet for safety. */
	cm = CMSG_NXTHDR(&hdr, cm);
	cm->cmsg_level = IPPROTO_IPV6;
	cm->cmsg_type = IPV6_HOPLIMIT;
	cm->cmsg_len = CMSG_LEN(sizeof(int));
	memcpy(CMSG_DATA(cm), &hoplimit, sizeof(int));

	return (sendmsg(sock, &hdr, 0));
}

static int
probe_defrouters(uint32_t ifindex, uint32_t linkid)
{
	static int probesock = -1;
	struct sockaddr_in6 dst;
	struct in6_defrouter *p, *ep;
	char *buf;
	size_t len;
	int mib[4];

	if (ifindex == 0)
		return (0);
	if (getsocket(&probesock, IPPROTO_NONE) != 0)
		return (-1);

	mib[0] = CTL_NET;
	mib[1] = PF_INET6;
	mib[2] = IPPROTO_ICMPV6;
	mib[3] = ICMPV6CTL_ND6_DRLIST;
	if (sysctl(mib, nitems(mib), NULL, &len, NULL, 0) < 0)
		return (-1);
	if (len == 0)
		return (0);

	memset(&dst, 0, sizeof(dst));
	dst.sin6_family = AF_INET6;
	dst.sin6_len = sizeof(dst);

	buf = malloc(len);
	if (buf == NULL)
		return (-1);
	if (sysctl(mib, nitems(mib), buf, &len, NULL, 0) < 0)
		return (-1);
	ep = (struct in6_defrouter *)(void *)(buf + len);
	for (p = (struct in6_defrouter *)(void *)buf; p < ep; p++) {
		if (ifindex != p->if_index)
			continue;
		if (!IN6_IS_ADDR_LINKLOCAL(&p->rtaddr.sin6_addr))
			continue;
		dst.sin6_addr = p->rtaddr.sin6_addr;
		dst.sin6_scope_id = linkid;
		(void)sendpacket(probesock, &dst, ifindex, 1, NULL, 0);
	}
	free(buf);

	return (0);
}

static int
rssend(uint32_t ifindex, uint32_t linkid, const void *data, size_t len)
{
	static int rssock = -1;
	struct sockaddr_in6 dst;
	ssize_t n;

	if (getsocket(&rssock, IPPROTO_ICMPV6) != 0)
		return (-1);

	memset(&dst, 0, sizeof(dst));
	dst.sin6_family = AF_INET6;
	dst.sin6_len = sizeof(dst);
	dst.sin6_addr = (struct in6_addr)IN6ADDR_LINKLOCAL_ALLROUTERS_INIT;
	dst.sin6_scope_id = linkid;

	n = sendpacket(rssock, &dst, ifindex, 255, data, len);
	if (n < 0 || (size_t)n != len)
		return (-1);
	return (0);
}

int
cap_probe_defrouters(cap_channel_t *cap, struct ifinfo *ifinfo)
{
#ifdef WITH_CASPER
	nvlist_t *nvl;
	int error;

	nvl = nvlist_create(0);
	nvlist_add_string(nvl, "cmd", "probe_defrouters");
	nvlist_add_number(nvl, "ifindex", ifinfo->sdl->sdl_index);
	nvlist_add_number(nvl, "linkid", ifinfo->linkid);

	nvl = cap_xfer_nvlist(cap, nvl);
	if (nvl == NULL)
		return (errno);
	error = (int)dnvlist_get_number(nvl, "error", 0);
	nvlist_destroy(nvl);
	errno = error;
	return (error == 0 ? 0 : -1);
#else
	(void)cap;
	return (probe_defrouters(ifinfo->sdl->sdl_index, ifinfo->linkid));
#endif
}

int
cap_rssend(cap_channel_t *cap, struct ifinfo *ifinfo)
{
	int error;

#ifdef WITH_CASPER
	nvlist_t *nvl = nvlist_create(0);
	nvlist_add_string(nvl, "cmd", "rssend");
	nvlist_add_number(nvl, "ifindex", ifinfo->sdl->sdl_index);
	nvlist_add_number(nvl, "linkid", ifinfo->linkid);
	nvlist_add_binary(nvl, "data", ifinfo->rs_data, ifinfo->rs_datalen);

	nvl = cap_xfer_nvlist(cap, nvl);
	if (nvl == NULL)
		return (errno);
	error = (int)dnvlist_get_number(nvl, "error", 0);
	nvlist_destroy(nvl);
	errno = error;
#else
	(void)cap;
	error = rssend(ifinfo->sdl->sdl_index, ifinfo->linkid, ifinfo->rs_data,
	    ifinfo->rs_datalen);
#endif

	ifinfo->probes++;
	if (error != 0 && (errno != ENETDOWN || dflag > 0)) {
		error = errno;
		warnmsg(LOG_ERR, __func__, "sendmsg on %s: %s",
		    ifinfo->ifname, strerror(errno));
		errno = error;
	}
	return (error == 0 ? 0 : -1);
}

#ifdef WITH_CASPER
static int
sendmsg_command(const char *cmd, const nvlist_t *limits __unused, nvlist_t *nvlin,
    nvlist_t *nvlout __unused)
{
	const void *data;
	size_t len;
	uint32_t ifindex, linkid;
	int error;

	if (strcmp(cmd, "probe_defrouters") != 0 &&
	    strcmp(cmd, "rssend") != 0)
		return (EINVAL);

	ifindex = (uint32_t)nvlist_get_number(nvlin, "ifindex");
	linkid = (uint32_t)nvlist_get_number(nvlin, "linkid");
	if (strcmp(cmd, "probe_defrouters") == 0) {
		error = probe_defrouters(ifindex, linkid);
	} else {
		data = nvlist_get_binary(nvlin, "data", &len);
		error = rssend(ifindex, linkid, data, len);
	}
	if (error != 0)
		return (errno);
	return (0);
}

CREATE_SERVICE("rtsold.sendmsg", NULL, sendmsg_command, 0);
#endif /* WITH_CASPER */
