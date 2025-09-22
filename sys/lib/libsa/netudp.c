/*	$OpenBSD: netudp.c,v 1.4 2018/03/31 17:09:56 patrick Exp $	*/
/*	$NetBSD: net.c,v 1.14 1996/10/13 02:29:02 christos Exp $	*/

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
#include <netinet/ip_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>

#include "stand.h"
#include "net.h"

/* Caller must leave room for ethernet, ip and udp headers in front!! */
ssize_t
sendudp(struct iodesc *d, void *pkt, size_t len)
{
	ssize_t cc;
	struct ip *ip;
	struct udpiphdr *ui;
	struct udphdr *uh;
	u_char *ea;
	struct ip tip;

#ifdef NET_DEBUG
	if (debug) {
		printf("sendudp: d=%x called.\n", (u_int)d);
		if (d) {
			printf("saddr: %s:%d",
			    inet_ntoa(d->myip), ntohs(d->myport));
			printf(" daddr: %s:%d\n",
			    inet_ntoa(d->destip), ntohs(d->destport));
		}
	}
#endif

	uh = (struct udphdr *)pkt - 1;
	ip = (struct ip *)uh - 1;
	len += sizeof(*ip) + sizeof(*uh);

	bzero(ip, sizeof(*ip) + sizeof(*uh));

	ip->ip_v = IPVERSION;			/* half-char */
	ip->ip_hl = sizeof(*ip) >> 2;		/* half-char */
	ip->ip_len = htons(len);
	ip->ip_p = IPPROTO_UDP;			/* char */
	ip->ip_ttl = IP_TTL;			/* char */
	ip->ip_src = d->myip;
	ip->ip_dst = d->destip;
	ip->ip_sum = in_cksum(ip, sizeof(*ip));	 /* short, but special */

	uh->uh_sport = d->myport;
	uh->uh_dport = d->destport;
	uh->uh_ulen = htons(len - sizeof(*ip));

	/* Calculate checksum (must save and restore ip header) */
	tip = *ip;
	ui = (struct udpiphdr *)ip;
	bzero(ui->ui_x1, sizeof(ui->ui_x1));
	ui->ui_len = uh->uh_ulen;
	uh->uh_sum = in_cksum(ui, len);
	*ip = tip;

	if (ip->ip_dst.s_addr == INADDR_BROADCAST || ip->ip_src.s_addr == 0 ||
	    netmask == 0 || SAMENET(ip->ip_src, ip->ip_dst, netmask))
		ea = arpwhohas(d, ip->ip_dst);
	else
		ea = arpwhohas(d, gateip);

	cc = sendether(d, ip, len, ea, ETHERTYPE_IP);
	if (cc < 0)
		return (-1);
	if ((size_t)cc != len)
		panic("sendudp: bad write (%d != %d)", cc, len);
	return (cc - (sizeof(*ip) + sizeof(*uh)));
}

/*
 * Receive a UDP packet and validate it is for us.
 * Caller leaves room for the headers (Ether, IP, UDP)
 */
ssize_t
readudp(struct iodesc *d, void *pkt, size_t len, time_t tleft)
{
	ssize_t n;
	size_t hlen;
	struct ip *ip;
	struct udphdr *uh;
	struct udpiphdr *ui;
	struct ip tip;
	u_int16_t etype;	/* host order */

#ifdef NET_DEBUG
	if (debug)
		printf("readudp: called\n");
#endif

	uh = (struct udphdr *)pkt - 1;
	ip = (struct ip *)uh - 1;

	n = readether(d, ip, len + sizeof(*ip) + sizeof(*uh), tleft, &etype);
	if (n < 0 || (size_t)n < sizeof(*ip) + sizeof(*uh))
		return -1;

	/* Ethernet address checks now in readether() */

	/* Need to respond to ARP requests. */
	if (etype == ETHERTYPE_ARP) {
		struct arphdr *ah = (void *)ip;
		if (ah->ar_op == htons(ARPOP_REQUEST)) {
			/* Send ARP reply */
			arp_reply(d, ah);
		}
		return -1;
	}

	if (etype != ETHERTYPE_IP) {
#ifdef NET_DEBUG
		if (debug)
			printf("readudp: not IP. ether_type=%x\n", etype);
#endif
		return -1;
	}

	/* Check ip header */
	if (ip->ip_v != IPVERSION || ip->ip_p != IPPROTO_UDP) {	/* half char */
#ifdef NET_DEBUG
		if (debug)
			printf("readudp: IP version or not UDP. ip_v=%d ip_p=%d\n", ip->ip_v, ip->ip_p);
#endif
		return -1;
	}

	hlen = ip->ip_hl << 2;
	if (hlen < sizeof(*ip) || in_cksum(ip, hlen) != 0) {
#ifdef NET_DEBUG
		if (debug)
			printf("readudp: short hdr or bad cksum.\n");
#endif
		return -1;
	}
	if (n < ntohs(ip->ip_len)) {
#ifdef NET_DEBUG
		if (debug)
			printf("readudp: bad length %d < %d.\n",
			    n, ntohs(ip->ip_len));
#endif
		return -1;
	}
	if (d->myip.s_addr && ip->ip_dst.s_addr != d->myip.s_addr) {
#ifdef NET_DEBUG
		if (debug) {
			printf("readudp: bad saddr %s != ", inet_ntoa(d->myip));
			printf("%s\n", inet_ntoa(ip->ip_dst));
		}
#endif
		return -1;
	}

	/* If there were ip options, make them go away */
	if (hlen != sizeof(*ip)) {
		bcopy(((u_char *)ip) + hlen, uh, len - hlen);
		ip->ip_len = htons(sizeof(*ip));
		n -= hlen - sizeof(*ip);
	}
	if (uh->uh_dport != d->myport) {
#ifdef NET_DEBUG
		if (debug)
			printf("readudp: bad dport %d != %d\n",
				d->myport, ntohs(uh->uh_dport));
#endif
		return -1;
	}

	if (uh->uh_sum) {
		n = ntohs(uh->uh_ulen) + sizeof(*ip);
		if (n > RECV_SIZE - ETHER_SIZE) {
			printf("readudp: huge packet, udp len %ld\n", (long)n);
			return -1;
		}

		/* Check checksum (must save and restore ip header) */
		tip = *ip;
		ui = (struct udpiphdr *)ip;
		bzero(ui->ui_x1, sizeof(ui->ui_x1));
		ui->ui_len = uh->uh_ulen;
		if (in_cksum(ui, n) != 0) {
#ifdef NET_DEBUG
			if (debug)
				printf("readudp: bad cksum\n");
#endif
			*ip = tip;
			return -1;
		}
		*ip = tip;
	}
	if (ntohs(uh->uh_ulen) < sizeof(*uh)) {
#ifdef NET_DEBUG
		if (debug)
			printf("readudp: bad udp len %d < %d\n",
			    ntohs(uh->uh_ulen), sizeof(*uh));
#endif
		return -1;
	}

	n -= sizeof(*ip) + sizeof(*uh);
	return (n);
}
