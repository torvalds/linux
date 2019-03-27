/*	$NetBSD: rarp.c,v 1.16 1997/07/07 15:52:52 drochner Exp $	*/

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
 * @(#) Header: arp.c,v 1.5 93/07/15 05:52:26 leres Exp  (LBL)
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <netinet/in_systm.h>

#include <string.h>

#include "stand.h"
#include "net.h"
#include "netif.h"


static ssize_t rarpsend(struct iodesc *, void *, size_t);
static ssize_t rarprecv(struct iodesc *, void **, void **, time_t, void *);

/*
 * Ethernet (Reverse) Address Resolution Protocol (see RFC 903, and 826).
 */
int
rarp_getipaddress(int sock)
{
	struct iodesc *d;
	struct ether_arp *ap;
	void *pkt;
	struct {
		u_char header[ETHER_SIZE];
		struct {
			struct ether_arp arp;
			u_char pad[18]; 	/* 60 - sizeof(arp) */
		} data;
	} wbuf;

#ifdef RARP_DEBUG
 	if (debug)
		printf("rarp: socket=%d\n", sock);
#endif
	if (!(d = socktodesc(sock))) {
		printf("rarp: bad socket. %d\n", sock);
		return (-1);
	}
#ifdef RARP_DEBUG
 	if (debug)
		printf("rarp: d=%x\n", (u_int)d);
#endif

	bzero((char*)&wbuf.data, sizeof(wbuf.data));
	ap = &wbuf.data.arp;
	ap->arp_hrd = htons(ARPHRD_ETHER);
	ap->arp_pro = htons(ETHERTYPE_IP);
	ap->arp_hln = sizeof(ap->arp_sha); /* hardware address length */
	ap->arp_pln = sizeof(ap->arp_spa); /* protocol address length */
	ap->arp_op = htons(ARPOP_REVREQUEST);
	bcopy(d->myea, ap->arp_sha, 6);
	bcopy(d->myea, ap->arp_tha, 6);
	pkt = NULL;

	if (sendrecv(d,
	    rarpsend, &wbuf.data, sizeof(wbuf.data),
	    rarprecv, &pkt, (void *)&ap, NULL) < 0) {
		printf("No response for RARP request\n");
		return (-1);
	}

	bcopy(ap->arp_tpa, (char *)&myip, sizeof(myip));
#if 0
	/* XXX - Can NOT assume this is our root server! */
	bcopy(ap->arp_spa, (char *)&rootip, sizeof(rootip));
#endif
	free(pkt);

	/* Compute our "natural" netmask. */
	if (IN_CLASSA(myip.s_addr))
		netmask = IN_CLASSA_NET;
	else if (IN_CLASSB(myip.s_addr))
		netmask = IN_CLASSB_NET;
	else
		netmask = IN_CLASSC_NET;

	d->myip = myip;
	return (0);
}

/*
 * Broadcast a RARP request (i.e. who knows who I am)
 */
static ssize_t
rarpsend(struct iodesc *d, void *pkt, size_t len)
{

#ifdef RARP_DEBUG
 	if (debug)
		printf("rarpsend: called\n");
#endif

	return (sendether(d, pkt, len, bcea, ETHERTYPE_REVARP));
}

/*
 * Returns 0 if this is the packet we're waiting for
 * else -1 (and errno == 0)
 */
static ssize_t
rarprecv(struct iodesc *d, void **pkt, void **payload, time_t tleft,
    void *extra)
{
	ssize_t n;
	struct ether_arp *ap;
	void *ptr = NULL;
	uint16_t etype;		/* host order */

#ifdef RARP_DEBUG
 	if (debug)
		printf("rarprecv: ");
#endif

	n = readether(d, &ptr, (void **)&ap, tleft, &etype);
	errno = 0;	/* XXX */
	if (n == -1 || n < sizeof(struct ether_arp)) {
#ifdef RARP_DEBUG
		if (debug)
			printf("bad len=%d\n", n);
#endif
		free(ptr);
		return (-1);
	}

	if (etype != ETHERTYPE_REVARP) {
#ifdef RARP_DEBUG
		if (debug)
			printf("bad type=0x%x\n", etype);
#endif
		free(ptr);
		return (-1);
	}

	if (ap->arp_hrd != htons(ARPHRD_ETHER) ||
	    ap->arp_pro != htons(ETHERTYPE_IP) ||
	    ap->arp_hln != sizeof(ap->arp_sha) ||
	    ap->arp_pln != sizeof(ap->arp_spa) )
	{
#ifdef RARP_DEBUG
		if (debug)
			printf("bad hrd/pro/hln/pln\n");
#endif
		free(ptr);
		return (-1);
	}

	if (ap->arp_op != htons(ARPOP_REVREPLY)) {
#ifdef RARP_DEBUG
		if (debug)
			printf("bad op=0x%x\n", ntohs(ap->arp_op));
#endif
		free(ptr);
		return (-1);
	}

	/* Is the reply for our Ethernet address? */
	if (bcmp(ap->arp_tha, d->myea, 6)) {
#ifdef RARP_DEBUG
		if (debug)
			printf("unwanted address\n");
#endif
		free(ptr);
		return (-1);
	}

	/* We have our answer. */
#ifdef RARP_DEBUG
 	if (debug)
		printf("got it\n");
#endif
	*pkt = ptr;
	*payload = ap;
	return (n);
}
