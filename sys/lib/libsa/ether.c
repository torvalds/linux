/*	$OpenBSD: ether.c,v 1.10 2014/11/19 20:28:56 miod Exp $	*/
/*	$NetBSD: ether.c,v 1.8 1996/10/13 02:29:00 christos Exp $	*/

/*
 * Copyright (c) 1992 Regents of the University of California.
 * All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 * @(#) Header: net.c,v 1.9 93/08/06 19:32:15 leres Exp  (LBL)
 */

#include <sys/param.h>
#include <sys/socket.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>

#include "stand.h"
#include "net.h"
#include "netif.h"

/* Caller must leave room for ethernet header in front!! */
ssize_t
sendether(struct iodesc *d, void *pkt, size_t len, u_char *dea, int etype)
{
	ssize_t n;
	struct ether_header *eh;

#ifdef ETHER_DEBUG
	if (debug)
		printf("sendether: called\n");
#endif

	eh = (struct ether_header *)pkt - 1;
	len += sizeof(*eh);

	MACPY(d->myea, eh->ether_shost);		/* by byte */
	MACPY(dea, eh->ether_dhost);			/* by byte */
	eh->ether_type = htons(etype);

	n = netif_put(d, eh, len);
	if (n < 0 || (size_t)n < sizeof(*eh))
		return (-1);

	n -= sizeof(*eh);
	return (n);
}

/*
 * Get a packet of any Ethernet type, with our address or
 * the broadcast address.  Save the Ether type in arg 5.
 * NOTE: Caller must leave room for the Ether header.
 */
ssize_t
readether(struct iodesc *d, void *pkt, size_t len, time_t tleft,
    u_int16_t *etype)
{
	ssize_t n;
	struct ether_header *eh;

#ifdef ETHER_DEBUG
	if (debug)
		printf("readether: called\n");
#endif

	eh = (struct ether_header *)pkt - 1;
	len += sizeof(*eh);

	n = netif_get(d, eh, len, tleft);
	if (n < 0 || (size_t)n < sizeof(*eh))
		return (-1);

	/* Validate Ethernet address. */
	if (bcmp(d->myea, eh->ether_dhost, 6) != 0 &&
	    bcmp(bcea, eh->ether_dhost, 6) != 0) {
#ifdef ETHER_DEBUG
		if (debug)
			printf("readether: not ours (ea=%s)\n",
			    ether_sprintf(eh->ether_dhost));
#endif
		return (-1);
	}
	*etype = ntohs(eh->ether_type);

	n -= sizeof(*eh);
	return (n);
}

/*
 * Convert Ethernet address to printable (loggable) representation.
 */
static const char digits[] = "0123456789abcdef";
const char *
ether_sprintf(const u_char *ap)
{
	int i;
	static char etherbuf[18];
	char *cp = etherbuf;

	for (i = 0; i < 6; i++) {
		*cp++ = digits[*ap >> 4];
		*cp++ = digits[*ap++ & 0xf];
		*cp++ = ':';
	}
	*--cp = 0;
	return (etherbuf);
}
