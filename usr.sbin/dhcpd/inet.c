/*	$OpenBSD: inet.c,v 1.6 2016/02/06 23:50:10 krw Exp $	*/

/*
 * Subroutines to manipulate internet addresses in a safely portable
 * way...
 */

/*
 * Copyright (c) 1996 The Internet Software Consortium.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of The Internet Software Consortium nor the names
 *    of its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INTERNET SOFTWARE CONSORTIUM AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE INTERNET SOFTWARE CONSORTIUM OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This software has been written for the Internet Software Consortium
 * by Ted Lemon <mellon@fugue.com> in cooperation with Vixie
 * Enterprises.  To learn more about the Internet Software Consortium,
 * see ``http://www.vix.com/isc''.  To learn more about Vixie
 * Enterprises, see ``http://www.vix.com''.
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <arpa/inet.h>

#include <net/if.h>

#include <netinet/in.h>

#include <stdio.h>
#include <string.h>

#include "dhcp.h"
#include "tree.h"
#include "dhcpd.h"

/*
 * Return just the network number of an internet address...
 */
struct iaddr
subnet_number(struct iaddr addr, struct iaddr mask)
{
	struct iaddr rv;
	int i;

	rv.len = 0;

	/* Both addresses must have the same length... */
	if (addr.len != mask.len)
		return (rv);

	rv.len = addr.len;
	for (i = 0; i < rv.len; i++)
		rv.iabuf[i] = addr.iabuf[i] & mask.iabuf[i];
	return (rv);
}

/*
 * Combine a network number and a integer to produce an internet address.
 * This won't work for subnets with more than 32 bits of host address, but
 * maybe this isn't a problem.
 */
struct iaddr
ip_addr(struct iaddr subnet, struct iaddr mask, u_int32_t host_address)
{
	int i, j, k;
	u_int32_t swaddr;
	unsigned char habuf[sizeof(swaddr)];
	struct iaddr rv;

	swaddr = htonl(host_address);
	memcpy(habuf, &swaddr, sizeof(swaddr));

	/*
	 * Combine the subnet address and the host address.   If the
	 * host address is bigger than can fit in the subnet, return a
	 * zero-length iaddr structure.
	 */
	rv = subnet;
	j = rv.len - sizeof(habuf);
	for (i = sizeof(habuf) - 1; i >= 0; i--) {
		if (mask.iabuf[i + j]) {
			if (habuf[i] > (mask.iabuf[i + j] ^ 0xFF)) {
				rv.len = 0;
				return (rv);
			}
			for (k = i - 1; k >= 0; k--)
				if (habuf[k]) {
					rv.len = 0;
					return (rv);
				}
			rv.iabuf[i + j] |= habuf[i];
			break;
		} else
			rv.iabuf[i + j] = habuf[i];
	}

	return (rv);
}

u_int32_t
host_addr(struct iaddr addr, struct iaddr mask)
{
	int i;
	u_int32_t swaddr;
	struct iaddr rv;

	rv.len = 0;

	/* Mask out the network bits... */
	rv.len = addr.len;
	for (i = 0; i < rv.len; i++)
		rv.iabuf[i] = addr.iabuf[i] & ~mask.iabuf[i];

	/* Copy out up to 32 bits... */
	memcpy(&swaddr, &rv.iabuf[rv.len - sizeof(swaddr)], sizeof(swaddr));

	/* Swap it and return it. */
	return (ntohl(swaddr));
}

int
addr_eq(struct iaddr addr1, struct iaddr addr2)
{
	if (addr1.len != addr2.len)
		return (0);
	return (memcmp(addr1.iabuf, addr2.iabuf, addr1.len) == 0);
}

char *
piaddr(struct iaddr addr)
{
	static char pbuf[32];
	const char *s;

	if (addr.len == 0)
		strlcpy(pbuf, "<null address>", sizeof(pbuf));
	else {
		s = inet_ntop(AF_INET, &addr.iabuf, pbuf, sizeof pbuf);
		if (s == NULL)
			strlcpy(pbuf, "<invalid address>", sizeof(pbuf));
	}
	return (pbuf);
}
