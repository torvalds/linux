/*	$OpenBSD: device.c,v 1.18 2021/01/26 18:25:07 deraadt Exp $ */

/*
 * Copyright (c) 1993-95 Mats O Jansson.  All rights reserved.
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
 */

#include "os.h"
#include "common.h"
#include "device.h"
#include "mopdef.h"
#include "pf.h"

extern struct if_info	*iflist;		/* Interface List		*/

#ifdef	DEV_NEW_CONF
/*
 * Return ethernet address for interface
 */

void
deviceEthAddr(char *ifname, u_char *eaddr)
{
	struct sockaddr_dl	*sdl;
	struct ifaddrs		*ifap, *ifa;

	if (getifaddrs(&ifap) != 0) {
		syslog(LOG_ERR, "deviceEthAddr: getifaddrs: %m");
		exit(1);
	}

	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		sdl = (struct sockaddr_dl *)ifa->ifa_addr;
		if (sdl->sdl_family != AF_LINK || sdl->sdl_type != IFT_ETHER ||
		    sdl->sdl_alen != 6)
			continue;
		if (!strcmp(ifa->ifa_name, ifname)) {
			bcopy(LLADDR(sdl), eaddr, 6);
			freeifaddrs(ifap);
			return;
		}
	}

	syslog(LOG_ERR, "deviceEthAddr: Never saw interface `%s'!", ifname);
	exit(1);
}
#endif	/* DEV_NEW_CONF */

void
deviceOpen(char *ifname, u_short proto, int trans)
{
	struct if_info	*p, tmp;

	strncpy(tmp.if_name, ifname, sizeof(tmp.if_name) - 1);
	tmp.if_name[sizeof(tmp.if_name) - 1] = 0;
	tmp.iopen = pfInit;

	switch (proto) {
	case MOP_K_PROTO_RC:
		tmp.read = mopReadRC;
		tmp.fd   = mopOpenRC(&tmp, trans);
		break;
	case MOP_K_PROTO_DL:
		tmp.read = mopReadDL;
		tmp.fd   = mopOpenDL(&tmp, trans);
		break;
	default:
		break;
	}

	if (tmp.fd != -1) {
		p = malloc(sizeof(*p));
		if (p == 0) {
			syslog(LOG_ERR, "deviceOpen: malloc: %m");
			exit(1);
		}

		p->next = iflist;
		iflist = p;

		strlcpy(p->if_name, tmp.if_name, IFNAME_SIZE);
		p->iopen   = tmp.iopen;
		p->write   = pfWrite;
		p->read    = tmp.read;
		bzero(p->eaddr, sizeof(p->eaddr));
		p->fd      = tmp.fd;

#ifdef	DEV_NEW_CONF
		deviceEthAddr(p->if_name, &p->eaddr[0]);
#else
		p->eaddr[0] = tmp.eaddr[0];
		p->eaddr[1] = tmp.eaddr[1];
		p->eaddr[2] = tmp.eaddr[2];
		p->eaddr[3] = tmp.eaddr[3];
		p->eaddr[4] = tmp.eaddr[4];
		p->eaddr[5] = tmp.eaddr[5];
#endif	/* DEV_NEW_CONF */

#ifdef LINUX2_PF
		{
			int s;

			s = socket(AF_INET, SOCK_DGRAM, 0);
			pfEthAddr(s, p->if_name, &p->eaddr[0]);
			close(s);
		}
#endif
	}
}

void
deviceInitOne(char *ifname)
{
	char	interface[IFNAME_SIZE];
	struct if_info *p;
	int	trans;
#ifdef _AIX
	char	dev[IFNAME_SIZE];
	int	unit,j;

	unit = 0;
	for (j = 0; j < strlen(ifname); j++) {
		if (isalpha((unsigned char)ifname[j]))
			dev[j] = ifname[j];
		else
			if (isdigit((unsigned char)ifname[j])) {
				unit = unit * 10 + ifname[j] - '0';
				dev[j] = '\0';
			}
	}

	if ((strlen(dev) == 2) && (dev[0] == 'e') &&
	    ((dev[1] == 'n') || (dev[1] == 't')))
		snprintf(interface, sizeof(interface), "ent%d\0", unit);
	else
		snprintf(interface, sizeof(interface), "%s%d\0", dev, unit);
#else
	snprintf(interface, sizeof(interface), "%s", ifname);
#endif /* _AIX */

	/* Ok, init it just once */
	p = iflist;
	for (p = iflist; p; p = p->next)
		if (strcmp(p->if_name, interface) == 0)
			return;

	syslog(LOG_INFO, "Initialized %s", interface);

	/* Ok, get transport information */
	trans = pfTrans(interface);

#ifndef NORC
	/* Start with MOP Remote Console */
	switch (trans) {
	case TRANS_ETHER:
		deviceOpen(interface, MOP_K_PROTO_RC, TRANS_ETHER);
		break;
	case TRANS_8023:
		deviceOpen(interface, MOP_K_PROTO_RC, TRANS_8023);
		break;
	case TRANS_ETHER + TRANS_8023:
		deviceOpen(interface, MOP_K_PROTO_RC, TRANS_ETHER);
		deviceOpen(interface, MOP_K_PROTO_RC, TRANS_8023);
		break;
	case TRANS_ETHER + TRANS_8023 + TRANS_AND:
		deviceOpen(interface, MOP_K_PROTO_RC, TRANS_ETHER + TRANS_8023);
		break;
	}
#endif

#ifndef NODL
	/* and next MOP Dump/Load */
	switch (trans) {
	case TRANS_ETHER:
		deviceOpen(interface, MOP_K_PROTO_DL, TRANS_ETHER);
		break;
	case TRANS_8023:
		deviceOpen(interface, MOP_K_PROTO_DL, TRANS_8023);
		break;
	case TRANS_ETHER + TRANS_8023:
		deviceOpen(interface, MOP_K_PROTO_DL, TRANS_ETHER);
		deviceOpen(interface, MOP_K_PROTO_DL, TRANS_8023);
		break;
	case TRANS_ETHER + TRANS_8023 + TRANS_AND:
		deviceOpen(interface, MOP_K_PROTO_DL, TRANS_ETHER + TRANS_8023);
		break;
	}
#endif

}

/*
 * Initialize all "candidate" interfaces that are in the system
 * configuration list.  A "candidate" is up, not loopback and not
 * point to point.
 */
void
deviceInitAll()
{
#ifdef DEV_NEW_CONF
	struct sockaddr_dl	*sdl;
	struct ifaddrs		*ifap, *ifa;

	if (getifaddrs(&ifap) != 0) {
		syslog(LOG_ERR, "deviceInitAll: getifaddrs: %m");
		exit(1);
	}

	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		sdl = (struct sockaddr_dl *)ifa->ifa_addr;
		if (sdl->sdl_family != AF_LINK || sdl->sdl_type != IFT_ETHER ||
		    sdl->sdl_alen != 6)
			continue;
		if ((ifa->ifa_flags &
		    (IFF_UP | IFF_LOOPBACK | IFF_POINTOPOINT)) != IFF_UP)
			continue;
		deviceInitOne(ifa->ifa_name);
	}
	freeifaddrs(ifap);
#else
	struct ifaddrs	*ifap, *ifa;

	if (getifaddrs(&ifap) != 0) {
		syslog(LOG_ERR, "deviceInitAll: getifaddrs: %m");
		exit(1);
	}

	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		if (/*(ifa->ifa_flags & IFF_UP) == 0 ||*/
		    ifa->ifa_flags & IFF_LOOPBACK ||
		    ifa->ifa_flags & IFF_POINTOPOINT)
			continue;
		deviceInitOne(ifa->ifa_name);
	}
	freeifaddrs(ifap);
#endif
}
