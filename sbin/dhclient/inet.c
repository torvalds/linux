/*	$OpenBSD: inet.c,v 1.7 2004/05/04 21:48:16 deraadt Exp $	*/

/*
 * Subroutines to manipulate internet addresses in a safely portable
 * way...
 */

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "dhcpd.h"

/*
 * Return just the network number of an internet address...
 */
struct iaddr
subnet_number(struct iaddr addr, struct iaddr mask)
{
	struct iaddr rv;
	unsigned i;

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
 * Given a subnet number and netmask, return the address on that subnet
 * for which the host portion of the address is all ones (the standard
 * broadcast address).
 */
struct iaddr
broadcast_addr(struct iaddr subnet, struct iaddr mask)
{
	struct iaddr rv;
	unsigned i;

	if (subnet.len != mask.len) {
		rv.len = 0;
		return (rv);
	}

	for (i = 0; i < subnet.len; i++)
		rv.iabuf[i] = subnet.iabuf[i] | (~mask.iabuf[i] & 255);
	rv.len = subnet.len;

	return (rv);
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
	struct in_addr a;
	char *s;

	memcpy(&a, &(addr.iabuf), sizeof(struct in_addr));

	if (addr.len == 0)
		strlcpy(pbuf, "<null address>", sizeof(pbuf));
	else {
		s = inet_ntoa(a);
		if (s != NULL)
			strlcpy(pbuf, s, sizeof(pbuf));
		else
			strlcpy(pbuf, "<invalid address>", sizeof(pbuf));
	}
	return (pbuf);
}
