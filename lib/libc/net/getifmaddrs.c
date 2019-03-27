/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Bruce M. Simpson.
 * All rights reserved
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

#include "namespace.h"
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>

#include <errno.h>
#include <ifaddrs.h>
#include <stdlib.h>
#include <string.h>
#include "un-namespace.h"

#define	SALIGN	(sizeof(long) - 1)
#define	SA_RLEN(sa)	((sa)->sa_len ? (((sa)->sa_len + SALIGN) & ~SALIGN) : \
			    (SALIGN + 1))
#define	MAX_SYSCTL_TRY	5
#define	RTA_MASKS	(RTA_GATEWAY | RTA_IFP | RTA_IFA)

int
getifmaddrs(struct ifmaddrs **pif)
{
	int icnt = 1;
	int dcnt = 0;
	int ntry = 0;
	size_t len;
	size_t needed;
	int mib[6];
	int i;
	char *buf;
	char *data;
	char *next;
	char *p;
	struct ifma_msghdr *ifmam;
	struct ifmaddrs *ifa, *ift;
	struct rt_msghdr *rtm;
	struct sockaddr *sa;

	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;             /* protocol */
	mib[3] = 0;             /* wildcard address family */
	mib[4] = NET_RT_IFMALIST;
	mib[5] = 0;             /* no flags */
	do {
		if (sysctl(mib, 6, NULL, &needed, NULL, 0) < 0)
			return (-1);
		if ((buf = malloc(needed)) == NULL)
			return (-1);
		if (sysctl(mib, 6, buf, &needed, NULL, 0) < 0) {
			if (errno != ENOMEM || ++ntry >= MAX_SYSCTL_TRY) {
				free(buf);
				return (-1);
			}
			free(buf);
			buf = NULL;
		} 
	} while (buf == NULL);

	for (next = buf; next < buf + needed; next += rtm->rtm_msglen) {
		rtm = (struct rt_msghdr *)(void *)next;
		if (rtm->rtm_version != RTM_VERSION)
			continue;
		switch (rtm->rtm_type) {
		case RTM_NEWMADDR:
			ifmam = (struct ifma_msghdr *)(void *)rtm;
			if ((ifmam->ifmam_addrs & RTA_IFA) == 0)
				break;
			icnt++;
			p = (char *)(ifmam + 1);
			for (i = 0; i < RTAX_MAX; i++) {
				if ((RTA_MASKS & ifmam->ifmam_addrs &
				    (1 << i)) == 0)
					continue;
				sa = (struct sockaddr *)(void *)p;
				len = SA_RLEN(sa);
				dcnt += len;
				p += len;
			}
			break;
		}
	}

	data = malloc(sizeof(struct ifmaddrs) * icnt + dcnt);
	if (data == NULL) {
		free(buf);
		return (-1);
	}

	ifa = (struct ifmaddrs *)(void *)data;
	data += sizeof(struct ifmaddrs) * icnt;

	memset(ifa, 0, sizeof(struct ifmaddrs) * icnt);
	ift = ifa;

	for (next = buf; next < buf + needed; next += rtm->rtm_msglen) {
		rtm = (struct rt_msghdr *)(void *)next;
		if (rtm->rtm_version != RTM_VERSION)
			continue;

		switch (rtm->rtm_type) {
		case RTM_NEWMADDR:
			ifmam = (struct ifma_msghdr *)(void *)rtm;
			if ((ifmam->ifmam_addrs & RTA_IFA) == 0)
				break;

			p = (char *)(ifmam + 1);
			for (i = 0; i < RTAX_MAX; i++) {
				if ((RTA_MASKS & ifmam->ifmam_addrs &
				    (1 << i)) == 0)
					continue;
				sa = (struct sockaddr *)(void *)p;
				len = SA_RLEN(sa);
				switch (i) {
				case RTAX_GATEWAY:
					ift->ifma_lladdr =
					    (struct sockaddr *)(void *)data;
					memcpy(data, p, len);
					data += len;
					break;

				case RTAX_IFP:
					ift->ifma_name =
					    (struct sockaddr *)(void *)data;
					memcpy(data, p, len);
					data += len;
					break;

				case RTAX_IFA:
					ift->ifma_addr =
					    (struct sockaddr *)(void *)data;
					memcpy(data, p, len);
					data += len;
					break;

				default:
					data += len;
					break;
				}
				p += len;
			}
			ift->ifma_next = ift + 1;
			ift = ift->ifma_next;
			break;
		}
	}

	free(buf);

	if (ift > ifa) {
		ift--;
		ift->ifma_next = NULL;
		*pif = ifa;
	} else {
		*pif = NULL;
		free(ifa);
	}
	return (0);
}

void
freeifmaddrs(struct ifmaddrs *ifmp)
{

	free(ifmp);
}
