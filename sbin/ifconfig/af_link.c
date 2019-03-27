/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 */

#ifndef lint
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ifaddrs.h>
#include <unistd.h>

#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/ethernet.h>

#include "ifconfig.h"

static struct ifreq link_ridreq;

extern char *f_ether;

static void
link_status(int s __unused, const struct ifaddrs *ifa)
{
	/* XXX no const 'cuz LLADDR is defined wrong */
	struct sockaddr_dl *sdl;
	char *ether_format, *format_char;
	struct ifreq ifr;
	int n, rc, sock_hw;
	static const u_char laggaddr[6] = {0};

	sdl = (struct sockaddr_dl *) ifa->ifa_addr;
	if (sdl == NULL || sdl->sdl_alen == 0)
		return;

	if ((sdl->sdl_type == IFT_ETHER || sdl->sdl_type == IFT_L2VLAN ||
	    sdl->sdl_type == IFT_BRIDGE) && sdl->sdl_alen == ETHER_ADDR_LEN) {
		ether_format = ether_ntoa((struct ether_addr *)LLADDR(sdl));
		if (f_ether != NULL && strcmp(f_ether, "dash") == 0) {
			for (format_char = strchr(ether_format, ':');
			    format_char != NULL;
			    format_char = strchr(ether_format, ':'))
				*format_char = '-';
		}
		printf("\tether %s\n", ether_format);
	} else {
		n = sdl->sdl_nlen > 0 ? sdl->sdl_nlen + 1 : 0;
		printf("\tlladdr %s\n", link_ntoa(sdl) + n);
	}

	/*
	 * Best-effort (i.e. failures are silent) to get original
	 * hardware address, as read by NIC driver at attach time. Only
	 * applies to Ethernet NICs (IFT_ETHER). However, laggX
	 * interfaces claim to be IFT_ETHER, and re-type their component
	 * Ethernet NICs as IFT_IEEE8023ADLAG. So, check for both. If
	 * the MAC is zeroed, then it's actually a lagg.
	 */
	if ((sdl->sdl_type != IFT_ETHER &&
	    sdl->sdl_type != IFT_IEEE8023ADLAG) ||
	    sdl->sdl_alen != ETHER_ADDR_LEN)
		return;

	strncpy(ifr.ifr_name, ifa->ifa_name, sizeof(ifr.ifr_name));
	memcpy(&ifr.ifr_addr, ifa->ifa_addr, sizeof(ifa->ifa_addr->sa_len));
	ifr.ifr_addr.sa_family = AF_LOCAL;
	if ((sock_hw = socket(AF_LOCAL, SOCK_DGRAM, 0)) < 0) {
		warn("socket(AF_LOCAL,SOCK_DGRAM)");
		return;
	}
	rc = ioctl(sock_hw, SIOCGHWADDR, &ifr);
	close(sock_hw);
	if (rc != 0)
		return;

	/*
	 * If this is definitely a lagg device or the hwaddr
	 * matches the link addr, don't bother.
	 */
	if (memcmp(ifr.ifr_addr.sa_data, laggaddr, sdl->sdl_alen) == 0 ||
	    memcmp(ifr.ifr_addr.sa_data, LLADDR(sdl), sdl->sdl_alen) == 0)
		goto pcp;

	ether_format = ether_ntoa((const struct ether_addr *)
	    &ifr.ifr_addr.sa_data);
	if (f_ether != NULL && strcmp(f_ether, "dash") == 0) {
		for (format_char = strchr(ether_format, ':');
		     format_char != NULL;
		     format_char = strchr(ether_format, ':'))
			*format_char = '-';
	}
	printf("\thwaddr %s\n", ether_format);

pcp:
	if (ioctl(s, SIOCGLANPCP, (caddr_t)&ifr) == 0 &&
	    ifr.ifr_lan_pcp != IFNET_PCP_NONE)
		printf("\tpcp %d\n", ifr.ifr_lan_pcp);
}

static void
link_getaddr(const char *addr, int which)
{
	char *temp;
	struct sockaddr_dl sdl;
	struct sockaddr *sa = &link_ridreq.ifr_addr;

	if (which != ADDR)
		errx(1, "can't set link-level netmask or broadcast");
	if (!strcmp(addr, "random")) {
		sdl.sdl_len = sizeof(sdl);
		sdl.sdl_alen = ETHER_ADDR_LEN;
		sdl.sdl_nlen = 0;
		sdl.sdl_family = AF_LINK;
		arc4random_buf(&sdl.sdl_data, ETHER_ADDR_LEN);
		/* Non-multicast and claim it is locally administered. */
		sdl.sdl_data[0] &= 0xfc;
		sdl.sdl_data[0] |= 0x02;
	} else {
		if ((temp = malloc(strlen(addr) + 2)) == NULL)
			errx(1, "malloc failed");
		temp[0] = ':';
		strcpy(temp + 1, addr);
		sdl.sdl_len = sizeof(sdl);
		link_addr(temp, &sdl);
		free(temp);
	}
	if (sdl.sdl_alen > sizeof(sa->sa_data))
		errx(1, "malformed link-level address");
	sa->sa_family = AF_LINK;
	sa->sa_len = sdl.sdl_alen;
	bcopy(LLADDR(&sdl), sa->sa_data, sdl.sdl_alen);
}

static struct afswtch af_link = {
	.af_name	= "link",
	.af_af		= AF_LINK,
	.af_status	= link_status,
	.af_getaddr	= link_getaddr,
	.af_aifaddr	= SIOCSIFLLADDR,
	.af_addreq	= &link_ridreq,
};
static struct afswtch af_ether = {
	.af_name	= "ether",
	.af_af		= AF_LINK,
	.af_status	= link_status,
	.af_getaddr	= link_getaddr,
	.af_aifaddr	= SIOCSIFLLADDR,
	.af_addreq	= &link_ridreq,
};
static struct afswtch af_lladdr = {
	.af_name	= "lladdr",
	.af_af		= AF_LINK,
	.af_status	= link_status,
	.af_getaddr	= link_getaddr,
	.af_aifaddr	= SIOCSIFLLADDR,
	.af_addreq	= &link_ridreq,
};

static __constructor void
link_ctor(void)
{
	af_register(&af_link);
	af_register(&af_ether);
	af_register(&af_lladdr);
}
