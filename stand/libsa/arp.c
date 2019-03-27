/*	$NetBSD: arp.c,v 1.18 1997/07/07 15:52:49 drochner Exp $	*/

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

#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <netinet/in_systm.h>

#include <string.h>

#include "stand.h"
#include "net.h"

/* Cache stuff */
#define ARP_NUM 8			/* need at most 3 arp entries */

struct arp_list {
	struct in_addr	addr;
	u_char		ea[6];
} arp_list[ARP_NUM] = {
	/* XXX - net order `INADDR_BROADCAST' must be a constant */
	{ {0xffffffff}, BA }
};
int arp_num = 1;

/* Local forwards */
static	ssize_t arpsend(struct iodesc *, void *, size_t);
static	ssize_t arprecv(struct iodesc *, void **, void **, time_t, void *);

/* Broadcast an ARP packet, asking who has addr on interface d */
u_char *
arpwhohas(struct iodesc *d, struct in_addr addr)
{
	int i;
	struct ether_arp *ah;
	struct arp_list *al;
	void *pkt;
	struct {
		struct ether_header eh;
		struct {
			struct ether_arp arp;
			u_char pad[18]; 	/* 60 - sizeof(...) */
		} data;
	} wbuf;

	/* Try for cached answer first */
	for (i = 0, al = arp_list; i < arp_num; ++i, ++al)
		if (addr.s_addr == al->addr.s_addr)
			return (al->ea);

	/* Don't overflow cache */
	if (arp_num > ARP_NUM - 1) {
		arp_num = 1;	/* recycle */
		printf("arpwhohas: overflowed arp_list!\n");
	}

#ifdef ARP_DEBUG
 	if (debug)
 	    printf("arpwhohas: send request for %s\n", inet_ntoa(addr));
#endif

	bzero((char*)&wbuf.data, sizeof(wbuf.data));
	ah = &wbuf.data.arp;
	ah->arp_hrd = htons(ARPHRD_ETHER);
	ah->arp_pro = htons(ETHERTYPE_IP);
	ah->arp_hln = sizeof(ah->arp_sha); /* hardware address length */
	ah->arp_pln = sizeof(ah->arp_spa); /* protocol address length */
	ah->arp_op = htons(ARPOP_REQUEST);
	MACPY(d->myea, ah->arp_sha);
	bcopy(&d->myip, ah->arp_spa, sizeof(ah->arp_spa));
	/* Leave zeros in arp_tha */
	bcopy(&addr, ah->arp_tpa, sizeof(ah->arp_tpa));

	/* Store ip address in cache (incomplete entry). */
	al->addr = addr;

	pkt = NULL;
	ah = NULL;
	i = sendrecv(d,
	    arpsend, &wbuf.data, sizeof(wbuf.data),
	    arprecv, &pkt, (void **)&ah, NULL);
	if (i == -1) {
		panic("arp: no response for %s",
			  inet_ntoa(addr));
	}

	/* Store ethernet address in cache */
#ifdef ARP_DEBUG
 	if (debug) {
		struct ether_header *eh;

		eh = (struct ether_header *)((uintptr_t)pkt + ETHER_ALIGN);
		printf("arp: response from %s\n",
		    ether_sprintf(eh->ether_shost));
		printf("arp: cacheing %s --> %s\n",
		    inet_ntoa(addr), ether_sprintf(ah->arp_sha));
	}
#endif
	MACPY(ah->arp_sha, al->ea);
	++arp_num;

	free(pkt);
	return (al->ea);
}

static ssize_t
arpsend(struct iodesc *d, void *pkt, size_t len)
{

#ifdef ARP_DEBUG
 	if (debug)
		printf("arpsend: called\n");
#endif

	return (sendether(d, pkt, len, bcea, ETHERTYPE_ARP));
}

/*
 * Returns 0 if this is the packet we're waiting for
 * else -1 (and errno == 0)
 */
static ssize_t
arprecv(struct iodesc *d, void **pkt, void **payload, time_t tleft, void *extra)
{
	ssize_t n;
	struct ether_arp *ah;
	uint16_t etype;	/* host order */
	void *ptr;

#ifdef ARP_DEBUG
 	if (debug)
		printf("arprecv: ");
#endif

	ptr = NULL;
	n = readether(d, &ptr, (void **)&ah, tleft, &etype);
	errno = 0;	/* XXX */
	if (n == -1 || n < sizeof(struct ether_arp)) {
#ifdef ARP_DEBUG
		if (debug)
			printf("bad len=%d\n", n);
#endif
		free(ptr);
		return (-1);
	}

	if (etype != ETHERTYPE_ARP) {
#ifdef ARP_DEBUG
		if (debug)
			printf("not arp type=%d\n", etype);
#endif
		free(ptr);
		return (-1);
	}

	/* Ethernet address now checked in readether() */
	if (ah->arp_hrd != htons(ARPHRD_ETHER) ||
	    ah->arp_pro != htons(ETHERTYPE_IP) ||
	    ah->arp_hln != sizeof(ah->arp_sha) ||
	    ah->arp_pln != sizeof(ah->arp_spa) )
	{
#ifdef ARP_DEBUG
		if (debug)
			printf("bad hrd/pro/hln/pln\n");
#endif
		free(ptr);
		return (-1);
	}

	if (ah->arp_op == htons(ARPOP_REQUEST)) {
#ifdef ARP_DEBUG
		if (debug)
			printf("is request\n");
#endif
		arp_reply(d, ah);
		free(ptr);
		return (-1);
	}

	if (ah->arp_op != htons(ARPOP_REPLY)) {
#ifdef ARP_DEBUG
		if (debug)
			printf("not ARP reply\n");
#endif
		free(ptr);
		return (-1);
	}

	/* Is the reply from the source we want? */
	if (bcmp(&arp_list[arp_num].addr,
			 ah->arp_spa, sizeof(ah->arp_spa)))
	{
#ifdef ARP_DEBUG
		if (debug)
			printf("unwanted address\n");
#endif
		free(ptr);
		return (-1);
	}
	/* We don't care who the reply was sent to. */

	/* We have our answer. */
#ifdef ARP_DEBUG
 	if (debug)
		printf("got it\n");
#endif
	*pkt = ptr;
	*payload = ah;
	return (n);
}

/*
 * Convert an ARP request into a reply and send it.
 * Notes:  Re-uses buffer.  Pad to length = 46.
 */
void
arp_reply(struct iodesc *d, void *pkt)
{
	struct ether_arp *arp = pkt;

	if (arp->arp_hrd != htons(ARPHRD_ETHER) ||
	    arp->arp_pro != htons(ETHERTYPE_IP) ||
	    arp->arp_hln != sizeof(arp->arp_sha) ||
	    arp->arp_pln != sizeof(arp->arp_spa) )
	{
#ifdef ARP_DEBUG
		if (debug)
			printf("arp_reply: bad hrd/pro/hln/pln\n");
#endif
		return;
	}

	if (arp->arp_op != htons(ARPOP_REQUEST)) {
#ifdef ARP_DEBUG
		if (debug)
			printf("arp_reply: not request!\n");
#endif
		return;
	}

	/* If we are not the target, ignore the request. */
	if (bcmp(arp->arp_tpa, &d->myip, sizeof(arp->arp_tpa)))
		return;

#ifdef ARP_DEBUG
	if (debug) {
		printf("arp_reply: to %s\n", ether_sprintf(arp->arp_sha));
	}
#endif

	arp->arp_op = htons(ARPOP_REPLY);
	/* source becomes target */
	bcopy(arp->arp_sha, arp->arp_tha, sizeof(arp->arp_tha));
	bcopy(arp->arp_spa, arp->arp_tpa, sizeof(arp->arp_tpa));
	/* here becomes source */
	bcopy(d->myea,  arp->arp_sha, sizeof(arp->arp_sha));
	bcopy(&d->myip, arp->arp_spa, sizeof(arp->arp_spa));

	/*
	 * No need to get fancy here.  If the send fails, the
	 * requestor will just ask again.
	 */
	(void) sendether(d, pkt, sizeof(*arp) + 18,
	                 arp->arp_tha, ETHERTYPE_ARP);
}
