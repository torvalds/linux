/* Taken from $NetBSD: net.c,v 1.20 1997/12/26 22:41:30 scottr Exp $	*/

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
 * @(#) Header: net.c,v 1.9 93/08/06 19:32:15 leres Exp  (LBL)
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/socket.h>

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

/* Caller must leave room for ethernet, ip and udp headers in front!! */
ssize_t
sendudp(struct iodesc *d, void *pkt, size_t len)
{
	ssize_t cc;
	struct udpiphdr *ui;
	struct udphdr *uh;

#ifdef NET_DEBUG
 	if (debug) {
		printf("sendudp: d=%lx called.\n", (long)d);
		if (d) {
			printf("saddr: %s:%d",
			    inet_ntoa(d->myip), ntohs(d->myport));
			printf(" daddr: %s:%d\n",
			    inet_ntoa(d->destip), ntohs(d->destport));
		}
	}
#endif

	ui = (struct udpiphdr *)pkt - 1;
	bzero(ui, sizeof(*ui));

	uh = (struct udphdr *)pkt - 1;
	len += sizeof(*uh);

	uh->uh_sport = d->myport;
	uh->uh_dport = d->destport;
	uh->uh_ulen = htons(len);

	ui->ui_pr = IPPROTO_UDP;
	ui->ui_len = uh->uh_ulen;
	ui->ui_src = d->myip;
	ui->ui_dst = d->destip;

#ifndef UDP_NO_CKSUM
	uh->uh_sum = in_cksum(ui, len + sizeof (struct ip));
#endif

	cc = sendip(d, uh, len, IPPROTO_UDP);
	if (cc == -1)
		return (-1);
	if (cc != len)
		panic("sendudp: bad write (%zd != %zd)", cc, len);
	return (cc - sizeof(*uh));
}

/*
 * Receive a UDP packet and validate it is for us.
 */
ssize_t
readudp(struct iodesc *d, void **pkt, void **payload, time_t tleft)
{
	ssize_t n;
	struct udphdr *uh;
	void *ptr;

#ifdef NET_DEBUG
	if (debug)
		printf("readudp: called\n");
#endif

	uh = NULL;
	ptr = NULL;
	n = readip(d, &ptr, (void **)&uh, tleft, IPPROTO_UDP);
	if (n == -1 || n < sizeof(*uh) || n != ntohs(uh->uh_ulen)) {
		free(ptr);
		return (-1);
	}

	if (uh->uh_dport != d->myport) {
#ifdef NET_DEBUG
		if (debug)
			printf("readudp: bad dport %d != %d\n",
				d->myport, ntohs(uh->uh_dport));
#endif
		free(ptr);
		return (-1);
	}

#ifndef UDP_NO_CKSUM
	if (uh->uh_sum) {
		struct udpiphdr *ui;
		struct ip *ip;
		struct ip tip;

		n = ntohs(uh->uh_ulen) + sizeof(*ip);

		/* Check checksum (must save and restore ip header) */
		ip = (struct ip *)uh - 1;
		tip = *ip;
		ui = (struct udpiphdr *)ip;
		bzero(&ui->ui_x1, sizeof(ui->ui_x1));
		ui->ui_len = uh->uh_ulen;
		if (in_cksum(ui, n) != 0) {
#ifdef NET_DEBUG
			if (debug)
				printf("readudp: bad cksum\n");
#endif
			free(ptr);
			return (-1);
		}
		*ip = tip;
	}
#endif
	if (ntohs(uh->uh_ulen) < sizeof(*uh)) {
#ifdef NET_DEBUG
		if (debug)
			printf("readudp: bad udp len %d < %d\n",
				ntohs(uh->uh_ulen), (int)sizeof(*uh));
#endif
		free(ptr);
		return (-1);
	}

	n = (n > (ntohs(uh->uh_ulen) - sizeof(*uh))) ? 
	    ntohs(uh->uh_ulen) - sizeof(*uh) : n;
	*pkt = ptr;
	*payload = (void *)((uintptr_t)uh + sizeof(*uh));
	return (n);
}
