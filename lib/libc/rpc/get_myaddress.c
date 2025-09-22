/*	$OpenBSD: get_myaddress.c,v 1.16 2020/12/30 18:56:35 benno Exp $ */

/*
 * Copyright (c) 2010, Oracle America, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials
 *       provided with the distribution.
 *     * Neither the name of the "Oracle America, Inc." nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *   FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *   COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 *   INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 *   GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * get_myaddress.c
 *
 * Get client's IP address via ioctl.  This avoids using the yellowpages.
 */

#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/pmap_prot.h>
#include <rpc/rpc.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <net/if.h>
#include <netinet/in.h>
#include <ifaddrs.h>

/* 
 * don't use gethostbyname, which would invoke yellow pages
 *
 * Avoid loopback interfaces.  We return information from a loopback
 * interface only if there are no other possible interfaces.
 */
int
get_myaddress(struct sockaddr_in *addr)
{
	struct ifaddrs *ifap, *ifa;
	int loopback = 0, gotit = 0;

	if (getifaddrs(&ifap) != 0)
		return (-1);

  again:
	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		if ((ifa->ifa_flags & IFF_UP) &&
		    ifa->ifa_addr != NULL &&
		    ifa->ifa_addr->sa_family == AF_INET &&
		    (loopback == 1 && (ifa->ifa_flags & IFF_LOOPBACK))) {
			*addr = *((struct sockaddr_in *)ifa->ifa_addr);
			addr->sin_port = htons(PMAPPORT);
			gotit = 1;
			break;
		}
	}
	if (gotit == 0 && loopback == 0) {
		loopback = 1;
		goto again;
	}
	freeifaddrs(ifap);
	return (0);
}
DEF_WEAK(get_myaddress);
