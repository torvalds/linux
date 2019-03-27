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
 */

/*
 * The send and receive functions were originally implemented in udp.c and
 * moved here. Also it is likely some more cleanup can be done, especially
 * once we will implement the support for tcp.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/queue.h>

#include <string.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/in_systm.h>

#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>

#include "stand.h"
#include "net.h"

typedef STAILQ_HEAD(ipqueue, ip_queue) ip_queue_t;
struct ip_queue {
	void		*ipq_pkt;
	struct ip	*ipq_hdr;
	STAILQ_ENTRY(ip_queue) ipq_next;
};

/*
 * Fragment re-assembly queue.
 */
struct ip_reasm {
	struct in_addr	ip_src;
	struct in_addr	ip_dst;
	uint16_t	ip_id;
	uint8_t		ip_proto;
	uint8_t		ip_ttl;
	size_t		ip_total_size;
	ip_queue_t	ip_queue;
	void		*ip_pkt;
	struct ip	*ip_hdr;
	STAILQ_ENTRY(ip_reasm) ip_next;
};

STAILQ_HEAD(ire_list, ip_reasm) ire_list = STAILQ_HEAD_INITIALIZER(ire_list);

/* Caller must leave room for ethernet and ip headers in front!! */
ssize_t
sendip(struct iodesc *d, void *pkt, size_t len, uint8_t proto)
{
	ssize_t cc;
	struct ip *ip;
	u_char *ea;

#ifdef NET_DEBUG
	if (debug) {
		printf("sendip: proto: %x d=%p called.\n", proto, (void *)d);
		if (d) {
			printf("saddr: %s:%d",
			    inet_ntoa(d->myip), ntohs(d->myport));
			printf(" daddr: %s:%d\n",
			    inet_ntoa(d->destip), ntohs(d->destport));
		}
	}
#endif

	ip = (struct ip *)pkt - 1;
	len += sizeof(*ip);

	bzero(ip, sizeof(*ip));

	ip->ip_v = IPVERSION;			/* half-char */
	ip->ip_hl = sizeof(*ip) >> 2;		/* half-char */
	ip->ip_len = htons(len);
	ip->ip_p = proto;			/* char */
	ip->ip_ttl = IPDEFTTL;			/* char */
	ip->ip_src = d->myip;
	ip->ip_dst = d->destip;
	ip->ip_sum = in_cksum(ip, sizeof(*ip));	 /* short, but special */

	if (ip->ip_dst.s_addr == INADDR_BROADCAST || ip->ip_src.s_addr == 0 ||
	    netmask == 0 || SAMENET(ip->ip_src, ip->ip_dst, netmask))
		ea = arpwhohas(d, ip->ip_dst);
	else
		ea = arpwhohas(d, gateip);

	cc = sendether(d, ip, len, ea, ETHERTYPE_IP);
	if (cc == -1)
		return (-1);
	if (cc != len)
		panic("sendip: bad write (%zd != %zd)", cc, len);
	return (cc - sizeof(*ip));
}

static void
ip_reasm_free(struct ip_reasm *ipr)
{
	struct ip_queue *ipq;

	while ((ipq = STAILQ_FIRST(&ipr->ip_queue)) != NULL) {
		STAILQ_REMOVE_HEAD(&ipr->ip_queue, ipq_next);
		free(ipq->ipq_pkt);
		free(ipq);
	}
	free(ipr->ip_pkt);
	free(ipr);
}

static int
ip_reasm_add(struct ip_reasm *ipr, void *pkt, struct ip *ip)
{
	struct ip_queue *ipq, *prev, *p;

	if ((ipq = calloc(1, sizeof (*ipq))) == NULL)
		return (1);

	ipq->ipq_pkt = pkt;
	ipq->ipq_hdr = ip;

	prev = NULL;
	STAILQ_FOREACH(p, &ipr->ip_queue, ipq_next) {
		if ((ntohs(p->ipq_hdr->ip_off) & IP_OFFMASK) <
		    (ntohs(ip->ip_off) & IP_OFFMASK)) {
			prev = p;
			continue;
		}
		if (prev == NULL)
			break;

		STAILQ_INSERT_AFTER(&ipr->ip_queue, prev, ipq, ipq_next);
		return (0);
	}
	STAILQ_INSERT_HEAD(&ipr->ip_queue, ipq, ipq_next);
	return (0);
}

/*
 * Receive a IP packet and validate it is for us.
 */
static ssize_t
readipv4(struct iodesc *d, void **pkt, void **payload, time_t tleft,
    uint8_t proto)
{
	ssize_t n;
	size_t hlen;
	struct ether_header *eh;
	struct ip *ip;
	struct udphdr *uh;
	uint16_t etype;		/* host order */
	char *ptr;
	struct ip_reasm *ipr;
	struct ip_queue *ipq, *last;

#ifdef NET_DEBUG
	if (debug)
		printf("readip: called\n");
#endif

	ip = NULL;
	ptr = NULL;
	n = readether(d, (void **)&ptr, (void **)&ip, tleft, &etype);
	if (n == -1 || n < sizeof(*ip) + sizeof(*uh)) {
		free(ptr);
		return (-1);
	}

	/* Ethernet address checks now in readether() */

	/* Need to respond to ARP requests. */
	if (etype == ETHERTYPE_ARP) {
		struct arphdr *ah = (void *)ip;
		if (ah->ar_op == htons(ARPOP_REQUEST)) {
			/* Send ARP reply */
			arp_reply(d, ah);
		}
		free(ptr);
		errno = EAGAIN;	/* Call me again. */
		return (-1);
	}

	if (etype != ETHERTYPE_IP) {
#ifdef NET_DEBUG
		if (debug)
			printf("readip: not IP. ether_type=%x\n", etype);
#endif
		free(ptr);
		return (-1);
	}

	/* Check ip header */
	if (ip->ip_v != IPVERSION ||	/* half char */
	    ip->ip_p != proto) {
#ifdef NET_DEBUG
		if (debug) {
			printf("readip: IP version or proto. ip_v=%d ip_p=%d\n",
			    ip->ip_v, ip->ip_p);
		}
#endif
		free(ptr);
		return (-1);
	}

	hlen = ip->ip_hl << 2;
	if (hlen < sizeof(*ip) ||
	    in_cksum(ip, hlen) != 0) {
#ifdef NET_DEBUG
		if (debug)
			printf("readip: short hdr or bad cksum.\n");
#endif
		free(ptr);
		return (-1);
	}
	if (n < ntohs(ip->ip_len)) {
#ifdef NET_DEBUG
		if (debug)
			printf("readip: bad length %d < %d.\n",
			       (int)n, ntohs(ip->ip_len));
#endif
		free(ptr);
		return (-1);
	}
	if (d->myip.s_addr && ip->ip_dst.s_addr != d->myip.s_addr) {
#ifdef NET_DEBUG
		if (debug) {
			printf("readip: bad saddr %s != ", inet_ntoa(d->myip));
			printf("%s\n", inet_ntoa(ip->ip_dst));
		}
#endif
		free(ptr);
		return (-1);
	}

	/* Unfragmented packet. */
	if ((ntohs(ip->ip_off) & IP_MF) == 0 &&
	    (ntohs(ip->ip_off) & IP_OFFMASK) == 0) {
		uh = (struct udphdr *)((uintptr_t)ip + sizeof (*ip));
		/* If there were ip options, make them go away */
		if (hlen != sizeof(*ip)) {
			bcopy(((u_char *)ip) + hlen, uh, uh->uh_ulen - hlen);
			ip->ip_len = htons(sizeof(*ip));
			n -= hlen - sizeof(*ip);
		}

		n = (n > (ntohs(ip->ip_len) - sizeof(*ip))) ?
		    ntohs(ip->ip_len) - sizeof(*ip) : n;
		*pkt = ptr;
		*payload = (void *)((uintptr_t)ip + sizeof(*ip));
		return (n);
	}

	STAILQ_FOREACH(ipr, &ire_list, ip_next) {
		if (ipr->ip_src.s_addr == ip->ip_src.s_addr &&
		    ipr->ip_dst.s_addr == ip->ip_dst.s_addr &&
		    ipr->ip_id == ip->ip_id &&
		    ipr->ip_proto == ip->ip_p)
			break;
	}

	/* Allocate new reassembly entry */
	if (ipr == NULL) {
		if ((ipr = calloc(1, sizeof (*ipr))) == NULL) {
			free(ptr);
			return (-1);
		}

		ipr->ip_src = ip->ip_src;
		ipr->ip_dst = ip->ip_dst;
		ipr->ip_id = ip->ip_id;
		ipr->ip_proto = ip->ip_p;
		ipr->ip_ttl = MAXTTL;
		STAILQ_INIT(&ipr->ip_queue);
		STAILQ_INSERT_TAIL(&ire_list, ipr, ip_next);
	}

	if (ip_reasm_add(ipr, ptr, ip) != 0) {
		STAILQ_REMOVE(&ire_list, ipr, ip_reasm, ip_next);
		free(ipr);
		free(ptr);
		return (-1);
	}

	if ((ntohs(ip->ip_off) & IP_MF) == 0) {
		ipr->ip_total_size = (8 * (ntohs(ip->ip_off) & IP_OFFMASK));
		ipr->ip_total_size += n + sizeof (*ip);
		ipr->ip_total_size += sizeof (struct ether_header);

		ipr->ip_pkt = malloc(ipr->ip_total_size + 2);
		if (ipr->ip_pkt == NULL) {
			STAILQ_REMOVE(&ire_list, ipr, ip_reasm, ip_next);
			ip_reasm_free(ipr);
			return (-1);
		}
	}

	/*
	 * If we do not have re-assembly buffer ipr->ip_pkt, we are still
	 * missing fragments, so just restart the read.
	 */
	if (ipr->ip_pkt == NULL) {
		errno = EAGAIN;
		return (-1);
	}

	/*
	 * Walk the packet list in reassembly queue, if we got all the
	 * fragments, build the packet.
	 */
	n = 0;
	last = NULL;
	STAILQ_FOREACH(ipq, &ipr->ip_queue, ipq_next) {
		if ((ntohs(ipq->ipq_hdr->ip_off) & IP_OFFMASK) != n / 8) {
			STAILQ_REMOVE(&ire_list, ipr, ip_reasm, ip_next);
			ip_reasm_free(ipr);
			return (-1);
		}

		n += ntohs(ipq->ipq_hdr->ip_len) - (ipq->ipq_hdr->ip_hl << 2);
		last = ipq;
	}
	if ((ntohs(last->ipq_hdr->ip_off) & IP_MF) != 0) {
		errno = EAGAIN;
		return (-1);
	}

	ipq = STAILQ_FIRST(&ipr->ip_queue);
	/* Fabricate ethernet header */
	eh = (struct ether_header *)((uintptr_t)ipr->ip_pkt + 2);
	bcopy((void *)((uintptr_t)ipq->ipq_pkt + 2), eh, sizeof (*eh));

	/* Fabricate IP header */
	ipr->ip_hdr = (struct ip *)((uintptr_t)eh + sizeof (*eh));
	bcopy(ipq->ipq_hdr, ipr->ip_hdr, sizeof (*ipr->ip_hdr));
	ipr->ip_hdr->ip_hl = sizeof (*ipr->ip_hdr) >> 2;
	ipr->ip_hdr->ip_len = htons(n);
	ipr->ip_hdr->ip_sum = 0;
	ipr->ip_hdr->ip_sum = in_cksum(ipr->ip_hdr, sizeof (*ipr->ip_hdr));

	n = 0;
	ptr = (char *)((uintptr_t)ipr->ip_hdr + sizeof (*ipr->ip_hdr));
	STAILQ_FOREACH(ipq, &ipr->ip_queue, ipq_next) {
		char *data;
		size_t len;

		hlen = ipq->ipq_hdr->ip_hl << 2;
		len = ntohs(ipq->ipq_hdr->ip_len) - hlen;
		data = (char *)((uintptr_t)ipq->ipq_hdr + hlen);

		bcopy(data, ptr + n, len);
		n += len;
	}

	*pkt = ipr->ip_pkt;
	ipr->ip_pkt = NULL;	/* Avoid free from ip_reasm_free() */
	*payload = ptr;

	/* Clean up the reassembly list */
	while ((ipr = STAILQ_FIRST(&ire_list)) != NULL) {
		STAILQ_REMOVE_HEAD(&ire_list, ip_next);
		ip_reasm_free(ipr);
	}
	return (n);
}

/*
 * Receive a IP packet.
 */
ssize_t
readip(struct iodesc *d, void **pkt, void **payload, time_t tleft,
    uint8_t proto)
{
	time_t t;
	ssize_t ret = -1;

	t = getsecs();
	while ((getsecs() - t) < tleft) {
		errno = 0;
		ret = readipv4(d, pkt, payload, tleft, proto);
		if (ret >= 0)
			return (ret);
		/* Bubble up the error if it wasn't successful */
		if (errno != EAGAIN)
			return (-1);
	}
	/* We've exhausted tleft; timeout */
	errno = ETIMEDOUT;
	return (-1);
}
